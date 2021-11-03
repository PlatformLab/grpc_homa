// This file contains a program that can be run as both client
// and server to run randomized stress tests on the Homa integration
// for gRPC.

#include <netdb.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <cstdarg>
#include <optional>
#include <random>
#include <thread>
#include <vector>

#include <grpcpp/grpcpp.h>
#include "stress.grpc.pb.h"

#include "homa.h"
#include "homa_client.h"
#include "homa_listener.h"
#include "time_trace.h"
#include "util.h"

// Values of command-line arguments:
int firstServer = 0;
bool isServer = false;
int numClientThreads = 0;
int numServerPorts = 1;
int numServers = 1;
bool useHoma = true;

static thread_local const char *lastOp = "none";

// An instance of this class will make a random choice among small
// integers, but with some values more likely to be chosen than
// others.
class WeightedChoice {
public:
    WeightedChoice(int weight, ...);
    
    /**
     * Choose an integer value using the given weights. 
     * \param rng
     *      Random number generator to build upon
     * \return
     *      Returns a number in the range [0, n) where n is the total number
     *      of weights passed to the constructor. Each integer will be chosen
     *      with a probability proportional to its weight.
     */
    template <class T>
    int operator()(T& rng)
    {
        int sum = 0;
        int value = dist(rng);
        for (size_t i = 0; i < weights.size(); i++) {
            sum += weights[i];
            if (sum > value) {
                return i;
            }
        }
        fprintf(stderr, "WeightedChoice ran out of weights; returning %lu\n",
                weights.size() - 1);
        return weights.size() - 1;
    }
    
protected:
    // Constructor arguments
    std::vector<int> weights;
    
    // Used to generate a random integer in the range [0, sum), where
    // sum is the sum of all the entries in weights.
    std::uniform_int_distribution<int> dist;
};

/**
 * Constructor for WeightedChoice.
 * \param zeroWeight
 *      The relative weight with which the value 0 will be returned.
 * \param ...
 *      Relative weights for return values 1, 2, and so on. The arguments
 *      are terminated by a weight < 0.
 */
WeightedChoice::WeightedChoice(int zeroWeight ...)
    : weights()
    , dist(0, 5)
{
    va_list ap;
    int sum = zeroWeight;
    weights.push_back(zeroWeight);
    va_start(ap, zeroWeight);
    while (true) {
        int weight = va_arg(ap, int);
        if (weight < 0) {
            break;
        }
        sum += weight;
        weights.push_back(weight);
    }
    dist = std::uniform_int_distribution<int>(0, sum-1);
}

// Each instance records cumulative statistics for a single client thread.
static const int numOps = 6;
struct ClientStats {
    // Total number of times each operation type has been invoked.
    uint64_t ops[numOps];
    
    // Total number of data bytes output for each operation type.
    uint64_t outBytes[numOps];
    
    // Total number of data bytes received for each operation type.
    uint64_t inBytes[numOps];
};

std::vector<ClientStats> clientStats;

// An instance of this class contains stubs that issue RPCs to a
// particular target (server and port).
class Target {
public:
    // Stub for invoking this server.
    std::unique_ptr<stress::Stress::Stub> stub;
    
    // Name and port for the server node.
    char server[100];
    
    /**
     * Constructor for StressClients.
     * \param serverIndex
     *      Index of this server node within the cluster (0 means "node-0").
     * \param serverPort
     *      Port within server to receive requests.
     */
    Target(int serverIndex, int serverPort)
        : stub()
        , server()
    {
        snprintf(server, sizeof(server), "node-%d:%d",
                serverIndex, serverPort);
        if (useHoma) {
            stub = stress::Stress::NewStub(
                    HomaClient::createInsecureChannel(server));
        } else {
            stub = stress::Stress::NewStub(grpc::CreateChannel(server,
                grpc::InsecureChannelCredentials()));
        }
    }
    
    /**
     * Send a variable-size buffer, receive a variable-size buffer in return
     * \param requestLength
     *      Number of bytes in buffer to send (will be rounded down to
     *      multiple of 4).
     * \param responseLength
     *      Number of bytes in response buffer (will be rounded down to
     *      multiple of 4).
     */
    void Ping(int requestLength, int replyLength)
    {
        stress::Request request;
        stress::Response response;
        grpc::ClientContext context;
        
        request.set_requestitems(requestLength>>2);
        request.set_replyitems(replyLength>>2);
        for (int i = 0; i < requestLength>>2; i++) {
            request.add_data(i);
        }
        grpc::Status status = stub->Ping(&context, request, &response);
        if (!status.ok()) {
            printf("Ping RPC to %s failed: %s\n",
                    server, stringForStatus(&status).c_str());
            usleep(1000000);
        }
        if (response.data_size() != replyLength>>2) {
            printf("Ping returned %d bytes, expected %d\n",
                    response.data_size()*4, replyLength);
        }
    }
    
    /**
     * Send a stream of messages, receive a single response.
     * \param sizes
     *      Each entry specifies the length of a single outbound message
     *      (will be rounded down to multiple of 4).
     * \param replyLength
     *      Number of bytes in the response (will be rounded down to
     *      multiple of 4).
     */
    void StreamOut(std::vector<int> &sizes, int replyLength)
    {
        stress::StreamOutRequest request;
        stress::Response response;
        grpc::ClientContext context;
        std::unique_ptr<grpc::ClientWriter<stress::StreamOutRequest>> writer(
                stub->StreamOut(&context, &response));
        
        for (size_t i = 0; i < sizes.size(); i++) {
            if (i == (sizes.size() - 1)) {
                request.set_done(1);
                request.set_replyitems(replyLength>>2);
            } else {
                request.set_done(0);
            }
            int size = sizes[i];
            request.clear_data();
            for (int i = 0; i < size>>2; i++) {
                request.add_data(i);
            }
            request.set_requestitems(size>>2);
            if (!writer->Write(request)) {
                break;
            }
        }
        writer->WritesDone();
        grpc::Status status = writer->Finish();
        if (!status.ok()) {
            printf("StreamOut RPC failed: %s\n",
                    stringForStatus(&status).c_str());
            usleep(1000000);
            return;
        }
        if (response.data_size() != replyLength>>2) {
            printf("StreamOut returned %d bytes, expected %d\n",
                    response.data_size()*4, replyLength);
        }
    }
    
    /**
     * Receive a stream of response messages.
     * \param sizes
     *      Each entry specifies the length of a single inbound message
     *      (will be rounded down to multiple of 4).
     */
    void StreamIn(std::vector<int> &sizes)
    {
        stress::StreamInRequest request;
        stress::Response response;
        grpc::ClientContext context;
        
        for (int size: sizes) {
            request.add_sizes(size>>2);
        }
        std::unique_ptr<grpc::ClientReader<stress::Response> > reader(
                stub->StreamIn(&context, request));
        int index = 0;
        while (reader->Read(&response)) {
            if (response.data_size() != sizes[index]>>2) {
                printf("StreamIn message %d contained %d bytes, expected %d\n",
                        index, response.data_size()*4, sizes[index]);
            }
//            gpr_log(GPR_INFO, "StreamIn received message with %d bytes",
//                4*response.data_size());
            index++;
        }
        if (index != static_cast<int>(sizes.size())) {
            printf("StreamIn received only %d responses, expected %lu\n",
                    index, sizes.size());
        }
        grpc::Status status = reader->Finish();
        if (!status.ok()) {
            printf("StreamIn failed: %s\n", stringForStatus(&status).c_str());
        }
    }
    
    /**
     * Receive a stream of response messages.
     * \param outSizes
     *      Each entry specifies the length of a single outbound message
     *      (will be rounded down to multiple of 4).
     * \param inSizes
     *      Each entry specifies the length of a single inbound message
     *      that will be returned in response to the corresponding entry in
     *      outSizes. A zero value means no inbound message will be returned
     *      at this point.
     */
    void Stream2Way(std::vector<int> &outSizes, std::vector<int> &inSizes)
    {
        stress::StreamOutRequest request;
        stress::Response response;
        grpc::ClientContext context;
        std::shared_ptr<grpc::ClientReaderWriter<stress::StreamOutRequest,
                stress::Response>> stream(stub->Stream2Way(&context));
        
        if (outSizes.size() != inSizes.size()) {
            printf("Stream2Way received mismatched sizes: %lu and %lu\n",
                outSizes.size(), inSizes.size());
            return;
        }
        
        for (size_t i = 0; i < outSizes.size(); i++) {
            int size = outSizes[i];
            if (i == (outSizes.size() - 1)) {
                request.set_done(1);
            } else {
                request.set_done(0);
            }
            request.set_requestitems(size>>2);
            request.set_replyitems(inSizes[i]>>2);
            request.clear_data();
            for (int j = 0; j < size>>2; j++) {
                request.add_data(j);
            }
            if (!stream->Write(request)) {
                break;
            }
            if (inSizes[i] >= 4) {
                if (!stream->Read(&response)) {
                    break;
                }
                if (response.data_size() != inSizes[i]>>2) {
                    printf("Stream2Way response %lu contained %d bytes, "
                            "expected %d\n",
                            i, response.data_size()*4, inSizes[i]);
                }
//                gpr_log(GPR_INFO, "Stream2Way received message with %d bytes",
//                    4*response.data_size());
            }
        }
        stream->WritesDone();
        grpc::Status status = stream->Finish();
        if (!status.ok()) {
            printf("Stream2Way RPC failed: %s\n",
                    stringForStatus(&status).c_str());
        }
    }
};

// This class implements the server side of the benchmark RPCs
class StressService : public stress::Stress::Service {   
public:    
    grpc::Status Ping(grpc::ServerContext*context,
            const stress::Request *request,
            stress::Response *response) override
    {
        lastOp = "Ping";
        if (request->data_size() != request->requestitems()) {
            printf("Expected %d items in Ping request, got %d\n",
                    request->requestitems(), request->data_size());
        }
        int length = request->replyitems();
        for (int i = 0; i < length; i++) {
            response->add_data(i);
        }
        return grpc::Status::OK;
    }
    
    grpc::Status StreamOut(grpc::ServerContext* context,
            grpc::ServerReader<stress::StreamOutRequest>* reader,
            stress::Response *response)
            override
    {
        lastOp = "StreamOut";
        stress::StreamOutRequest request;
        while (reader->Read(&request)) {
            if (request.data_size() != request.requestitems()) {
                printf("Expected %d items in StreamOut request, got %d\n",
                        request.requestitems(), request.data_size());
            }
            if (request.done()) {
                break;
            }
        }
        int length = request.replyitems();
        for (int i = 0; i < length; i++) {
            response->add_data(i);
        }
        return grpc::Status::OK;
    }
    
    grpc::Status StreamIn(grpc::ServerContext* context,
            const stress::StreamInRequest *request,
            grpc::ServerWriter<stress::Response>* writer)
            override
    {
        lastOp = "StreamIn";
        stress::Response response;
        for (int i = 0; i < request->sizes_size(); i ++) {
            response.clear_data();
            int size = request->sizes(i);
            for (int j = 0; j < size; j++) {
                response.add_data(j);
            }
            if (size != 0) {
                writer->Write(response);
            }
        }
        return grpc::Status::OK;
    }
    
    grpc::Status Stream2Way(grpc::ServerContext* context,
            grpc::ServerReaderWriter<stress::Response,
            stress::StreamOutRequest>* stream)
            override
    {
        lastOp = "Stream2Way";
        stress::StreamOutRequest request;
        stress::Response response;
        while (stream->Read(&request)) {
            if (request.data_size() != request.requestitems()) {
                printf("Expected %d items in Stream2Way request, got %d\n",
                        request.requestitems(), request.data_size());
            }
            response.clear_data();
            int size = request.replyitems();
            for (int i = 0; i < size; i++) {
                response.add_data(i);
            }
            stream->Write(response);
            if (request.done()) {
                break;
            }
        }
        return grpc::Status::OK;
    }
};

// One element for each combination of server node and port.
std::vector<Target> targets;

/**
 * Convenience function for creating a vector and loading it with values.
 * \param numValues
 *      Total number of entries to append to the vector.
 * \param ...
 *      numValues entries of type T, which will be appended to the new vector.
 */
template <class T>
std::vector<T> makeVector(int numValues, ...)
{
    std::vector<T> result;
    va_list ap;
    va_start(ap, numValues);
    while (numValues > 0) {
        T value = va_arg(ap, T);
        result.push_back(value);
        numValues--;
    }
    return result;
}


/**
 * Returns the sum of all of the values in an integer vector.
 * \param v
 *      Vector containing values to add up.
 */
int sum(std::vector<int>& v)
{
    int sum = 0;
    for (int i: v) {
        sum += i;
    }
    return sum;
}

/**
 * The top-level method for each client thread.
 * \param id
 *      Index of this thread among all client threads.
 */
void client(int id)
{
    std::mt19937 rng;
    rng.seed(id);
    ClientStats *stats = &clientStats[id];
    
    // Picks the target for an operation
    std::uniform_int_distribution<int> targetDist(0, targets.size()-1);
    
    // Selects a particular kind of operation (small, stream out, etc.)
    WeightedChoice typeDist(100, 10, 5, 5, 5, 2, -1);
    
    std::vector<int> streamSizes = makeVector<int>(
            5, 100, 5000, 1200000, 400, 50000);
    std::vector<int> stream2WayOutSizes = makeVector<int>(
            7, 100, 200, 1200000, 3000, 10000, 400, 50000);
    std::vector<int> stream2WayInSizes = makeVector<int>(
            7, 1200000, 5000, 300, 9000, 0, 0, 5000);
    
    while (true) {
        Target& target = targets[targetDist(rng)];
        int type = typeDist(rng);
        stats->ops[type]++;
        gpr_log(GPR_INFO, "Starting request, client %d type %d",
                id, type);
        switch (type) {
            // Issue a small ping request.
            case 0: {
                target.Ping(100, 100);
                stats->outBytes[type] += 100;
                stats->inBytes[type] += 100;
                break;
            }

            // Issue a medium-size ping request.
            case 1: {
                target.Ping(5000, 5000);
                stats->outBytes[type] += 5000;
                stats->inBytes[type] += 5000;
                break;
            }

            // Issue a large ping request.
            case 2: {
                target.Ping(1200000, 1200000);
                stats->outBytes[type] += 1200000;
                stats->inBytes[type] += 1200000;
                break;
            }

            // Stream data out to a server.
            case 3: {
                target.StreamOut(streamSizes, 5000);
                stats->outBytes[type] += sum(streamSizes);
                stats->inBytes[type] += 5000;
                break;
            }

            // Stream data in from a server.
            case 4: {
                target.StreamIn(streamSizes);
                stats->inBytes[type] += sum(streamSizes);
                break;
            }

            // Issue a request with streaming in both directions.
            case 5: {
                target.Stream2Way(stream2WayOutSizes, stream2WayInSizes);
                stats->outBytes[type] += sum(stream2WayOutSizes);
                stats->inBytes[type] += sum(stream2WayInSizes);
                break;
            }
        }
    }
}

// Human-readable name for each of the client operations.
const char *opNames[] = {
    "Short ping",
    "Medium ping",
    "Long ping",
    "Stream out",
    "Stream in",
    "Stream 2-way"
};

/**
 * Print help information for this program.
 */
void printHelp()
{
    
	printf("Usage: stress [option option ...]\n\n");
    printf("Exercises gRPC by invoking a variety of different requests "
            "at random.\n");
    printf("The following command-line options are supported:\n");
    printf("    --client-threads     How many client threads should issue "
            " requests from\n");
    printf("                         this node (default: 0)\n");
    printf("    --first-server       Index of first server node"
            "(default: 0)\n");
    printf("    --help               Print this message and exit\n");
    printf("    --is-server          This node should act as server (no "
            "argument,\n");
    printf("                         default is false)\n");
    printf("    --server-ports       How many ports will be open on each "
            "server for\n");
    printf("                         receiving requests (default: 1)\n");
    printf("    --servers            How many nodes will be acting as servers "
            "(default: 1)\n");
    printf("    --tcp                Use TCP for transport instead of Homa\n");
}



/**
 * Format a string in printf-style and append it to a std::string.
 * \param s
 *      Append the result to this string.
 * \param separator
 *      If non-NULL, and if s is non-empty, append this string to s before
 *      the new info.
 * \param format
 *      Standard printf-style format string.
 * \param ...
 *      Additional arguments as required by @format.
 */
void appendPrintf(std::string& s, const char *separator, const char* format, ...)
{
	va_list ap;
	va_start(ap, format);
	
	if (!s.empty() && (separator != NULL))
		s.append(separator);
	    
	// We're not really sure how big of a buffer will be necessary.
	// Try 1K, if not the return value will tell us how much is necessary.
	int buf_size = 1024;
	while (true) {
		char buf[buf_size];
		// vsnprintf trashes the va_list, so copy it first
		va_list aq;
		__va_copy(aq, ap);
		int length = vsnprintf(buf, buf_size, format, aq);
		assert(length >= 0); // old glibc versions returned -1
		if (length < buf_size) {
			s.append(buf, length);
			break;
		}
		buf_size = length + 1;
	}
	va_end(ap);
}

int main(int argc, char** argv)
{
    recordFunc = TimeTrace::record2;
    std::vector<std::string> args;
    unsigned nextArg;
    
    for (int i = 0; i < argc; i++) {
        args.emplace_back(argv[i]);
    }
    
    for (nextArg = 1; nextArg < args.size(); nextArg++) {
		const char *option = args[nextArg].c_str();
        if (strcmp(option, "--client-threads") == 0) {
            if (!parse(args, nextArg+1, &numClientThreads, option,
                    "integer")) {
				exit(1);
            }
			nextArg++;
        } else if (strcmp(option, "--first-server") == 0) {
            if (!parse(args, nextArg+1, &firstServer, option, "integer")) {
				exit(1);
            }
			nextArg++;
        } else if (strcmp(option, "--help") == 0) {
            printHelp();
            exit(0);
        } else if (strcmp(option, "--is-server") == 0) {
            isServer = true;
        } else if (strcmp(option, "--server-ports") == 0) {
            if (!parse(args, nextArg+1, &numServerPorts, option, "integer")) {
				exit(1);
            }
			nextArg++;
        } else if (strcmp(option, "--servers") == 0) {
            if (!parse(args, nextArg+1, &numServers, option, "integer")) {
				exit(1);
            }
            if (numServers < 1) {
                printf("Bad --servers option %d: must be at least 1\n",
                        numServers);
                exit(1);
            }
			nextArg++;
        } else if (strcmp(option, "--tcp") == 0) {
            useHoma = false;
        } else {
            printf("Unknown option %s\n", option);
            printHelp();
            exit(1);
        }
    }
    if ((numClientThreads == 0) && (numServerPorts == 0)) {
        printf("This machine isn't either a client or a server; exiting\n");
        exit(0);
    }
    
    StressService service;
    grpc::ServerBuilder builder;
    std::unique_ptr<grpc::Server> server;
    if (isServer) {
        for (int i = 0; i < numServerPorts; i++) {
            char address[100];
            
            if (useHoma) {
                snprintf(address, sizeof(address), "homa:%d", 4000+i);
                builder.AddListeningPort(address,
                        HomaListener::insecureCredentials());
            } else {
                snprintf(address, sizeof(address), "0.0.0.0:%d", 4000+i);
                builder.AddListeningPort(address,
                        grpc::InsecureServerCredentials());
                
            }
        }
        builder.RegisterService(&service);
        server = builder.BuildAndStart();
        if (server == nullptr) {
            exit(1);
        }
    }
    
    for (int i = 0; i < numServers; i++) {
        for (int j = 0; j < numServerPorts; j++) {
            targets.emplace_back(firstServer + i, 4000+j);
        }
    }
    
    clientStats.resize(numClientThreads);
    memset(clientStats.data(), 0, clientStats.size() * sizeof(clientStats[0]));
    for (int i = 0; i < numClientThreads; i++) {
        std::thread t(client, i);
        t.detach();
    }
    
    // This array stores a copy of clientStats as of the last time we
    // printed statistics.
    std::vector<ClientStats> prevClientStats;
    prevClientStats.resize(numClientThreads);
    memset(prevClientStats.data(), 0,
            numClientThreads * sizeof(prevClientStats[0]));
    
    // This array stores combined stats for all the clients for the two
    // most recent time samples.
    ClientStats totalStats[2];
    
    // Index in totalStats of the most recent sample.
    int latestStats = 0;
    
    // Time at which last stats were gathered.
    uint64_t lastSampleTime = 0;
    
    // True means this is the first time through the loop (we don't have
    // any old data).
    bool firstTime = true;
    
    // Each iteration through this loop prints statistics, then sleeps a while.
    while (true) {
        latestStats = 1-latestStats;
        ClientStats *curTotal = &totalStats[latestStats];
        ClientStats *oldTotal = &totalStats[1-latestStats];
        
        // These arrays build up strings listing rates of ops and data
        // for each operation type.
        std::string count[numOps];
        std::string outBytes[numOps];
        std::string inBytes[numOps];
        
        uint64_t currentTime = TimeTrace::rdtsc();
        double elapsed = TimeTrace::toSeconds(currentTime - lastSampleTime);
        
        // Gather and print current statistics.
        memset(curTotal, 0, sizeof(*curTotal));
        for (size_t i = 0; i < clientStats.size(); i++) {
            ClientStats *cur = &clientStats[i];
            ClientStats *old = &prevClientStats[i];
            for (int j = 0; j < numOps; j++) {
                double opsPerSec = (cur->ops[j] - old->ops[j])/elapsed;
                appendPrintf(count[j], " ", "%.3f", opsPerSec*1e-3);
                curTotal->ops[j] += cur->ops[j];
                old->ops[j] = cur->ops[j];

                double outPerSec = (cur->outBytes[j] - old->outBytes[j])
                        /elapsed;
                appendPrintf(outBytes[j], " ", "%.2f", outPerSec*1e-6);
                curTotal->outBytes[j] += cur->outBytes[j];
                old->outBytes[j] = cur->outBytes[j];

                double inPerSec = (cur->inBytes[j] - old->inBytes[j])
                        /elapsed;
                appendPrintf(inBytes[j], " ", "%.2f", inPerSec*1e-6);
                curTotal->inBytes[j] += cur->inBytes[j];
                old->inBytes[j] = cur->inBytes[j];
            }
        }
        if (!firstTime && (clientStats.size() != 0)) {
            double totalOps = 0.0;
            double totalOut = 0.0;
            double totalIn = 0.0;
            for (int i = 0; i < numOps; i++) {
                printf("%s:\n", opNames[i]);
                double opsPerSec = (curTotal->ops[i] - oldTotal->ops[i])/elapsed;
                totalOps += curTotal->ops[i] - oldTotal->ops[i];
                printf("  kOps/sec: %.3f [%s]\n", opsPerSec*1e-3, count[i].c_str());
                double outPerSec = (curTotal->outBytes[i]
                        - oldTotal->outBytes[i])/elapsed;
                totalOut += curTotal->outBytes[i] - oldTotal->outBytes[i];
                printf("  MB/sec out: %.2f [%s]\n", outPerSec*1e-6,
                        outBytes[i].c_str());
                double inPerSec = (curTotal->inBytes[i]
                        - oldTotal->inBytes[i])/elapsed;
                totalIn += curTotal->inBytes[i] - oldTotal->inBytes[i];
                printf("  MB/sec in: %.2f [%s]\n", inPerSec*1e-6,
                        inBytes[i].c_str());
            }
            printf("Totals: %.1f kOps/sec, %.1f MB/sec out %.1f MB/sec in\n",
                    1e-03*totalOps/elapsed, 1e-06*totalOut/elapsed,
                    1e-06*totalOut/elapsed);
        }
        
        firstTime = false;
        lastSampleTime = currentTime;
        usleep(2000000);
    }
    
    exit(0);
}