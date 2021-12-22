// This file contains a program that can be run as both client
// and server to run randomized stress tests on the Homa integration
// for gRPC.

#include <netdb.h>
#include <stdio.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <cstdarg>
#include <optional>
#include <random>
#include <thread>
#include <vector>

#include <grpcpp/grpcpp.h>
#include "basic.grpc.pb.h"

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

// Dynamically allocated buffer used to hold circular log in memory.
char *logMemory = nullptr;

// Amount of memory allocated at logMemory.
size_t logSize = 0;

// Offset within logMemory at which to write the next bytes.
size_t logOffset = 0;

// True means the log has wrapped (so information after logOffset is valid).
bool logWrapped = false;

// True means the log has been printed at least once.
bool logPrinted = false;

/**
 * Add information to the in-memory log. Note: this function is not
 * thread-safe.
 * \param p
 *      First byte of information to add to the log.
 * \param length
 *      Number of bytes to add to the log.
 */
void appendToLog(const char *p, size_t length)
{
    if (logPrinted) {
        return;
    }
    if (logMemory == nullptr) {
        logSize = 1000000000;
        logMemory = new char[logSize];
    }
    while (length > (logSize - logOffset)) {
        size_t chunkSize = logSize - logOffset;
        memcpy(logMemory + logOffset, p, chunkSize);
        length -= chunkSize;
        p += chunkSize;
        logOffset = 0;
        logWrapped = true;
    }
    if (length > 0) {
        memcpy(logMemory + logOffset, p, length);
        logOffset += length;
        if (logOffset >= logSize) {
            logOffset = 0;
            logWrapped = true;
        }
    }
}

/**
 * This function captures calls to gpr_log and logs the information in
 * the in-memory log.
 * \param args
 *      Information from the original gpr_log call.
 */
void logToMemory(gpr_log_func_args* args)
{
    static std::mutex mutex;
    std::lock_guard<std::mutex> guard(mutex);
    
    thread_local int tid = 0;
    if (tid == 0) {
        tid = gettid();
    }
    
    gpr_timespec now = gpr_now(GPR_CLOCK_REALTIME);
    
    const char *file;
    const char *lastSlash = strrchr(args->file, '/');
    if (lastSlash == nullptr) {
      file = args->file;
    } else {
      file = lastSlash + 1;
    }
    
    char buffer[58];
    size_t size = snprintf(buffer, sizeof(buffer), "%s %lu.%u %7d %s:%d]",
            gpr_log_severity_string(args->severity), now.tv_sec, now.tv_nsec,
            tid, file, args->line);
    if (size < sizeof(buffer)) {
        memset(buffer+size, ' ', sizeof(buffer) - size);
    }
    appendToLog(buffer, sizeof(buffer));
    appendToLog(args->message, strlen(args->message));
    appendToLog("\n", 1);
}

/**
 * Print out the contents of the in-memory log to a file.
 * \param file
 *      Name of the file in which to print the log.
 */
void printLog(const char *file)
{
    if (logMemory == NULL) {
        fprintf(stderr, "Can't print log: in-memory logging isn't enabled\n");
        return;
    }
    logPrinted = true;
    FILE *f = fopen(file, "w");
    if (f == nullptr) {
        fprintf(stderr, "Couldn't open %s to print log: %s\n", file,
                strerror(errno));
        return;
    }
    
    if (logWrapped) {
        fwrite(logMemory + logOffset, 1, logSize - logOffset, f);
    }
    fwrite(logMemory, 1, logOffset, f);
    if (ferror(f)) {
        fprintf(stderr, "Error writing log to %s: %s\n", file, strerror(errno));
    }
    fclose(f);
}

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

// Number of distinct operation types.
static const int numOps = 6;

// Human-readable names for each of the client operations.
const char *opNames[] = {
    "Ping100", "Ping5000", "Ping1.2MB", "StreamOut", "StreamIn", "Stream2Way"
};

// Each instance records metrics for a single client thread.
struct ClientStats {
    
    // Total number of times each operation type has been invoked.
    uint64_t ops[numOps];
    
    // Total number of data bytes output for each operation type.
    uint64_t outBytes[numOps];
    
    // Total number of data bytes received for each operation type.
    uint64_t inBytes[numOps];
    
    // Number of RTT measurements retained for each operation type.
    static const int maxSamples = 10000;
    
    // RTT measurements from recent RPCs, separated by operation
    // type.
    uint64_t rtts[numOps][maxSamples];
    
    // Index of where to record the next RTT measurement for each
    // operation type.
    int nextSample[numOps];
};

std::vector<ClientStats> clientStats;

// An instance of this class contains stubs that issue RPCs to a
// particular target (server and port).
class Target {
public:
    // Stub for invoking this server.
    std::unique_ptr<basic::Basic::Stub> stub;
    
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
            stub = basic::Basic::NewStub(
                    HomaClient::createInsecureChannel(server));
        } else {
            stub = basic::Basic::NewStub(grpc::CreateChannel(server,
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
        basic::Request request;
        basic::Response response;
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
        basic::StreamOutRequest request;
        basic::Response response;
        grpc::ClientContext context;
        std::unique_ptr<grpc::ClientWriter<basic::StreamOutRequest>> writer(
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
        basic::StreamInRequest request;
        basic::Response response;
        grpc::ClientContext context;
        
        for (int size: sizes) {
            request.add_sizes(size>>2);
        }
        std::unique_ptr<grpc::ClientReader<basic::Response> > reader(
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
        basic::StreamOutRequest request;
        basic::Response response;
        grpc::ClientContext context;
        std::shared_ptr<grpc::ClientReaderWriter<basic::StreamOutRequest,
                basic::Response>> stream(stub->Stream2Way(&context));
        
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
    
    /**
     * Ask the peer to print its in-memory log, if it hasn't already
     * done so.
     */
    void PrintLog()
    {
        basic::Empty request;
        basic::Empty response;
        grpc::ClientContext context;
        
        grpc::Status status = stub->PrintLog(&context, request, &response);
        if (!status.ok()) {
            printf("PrintLog RPC to %s failed: %s\n",
                    server, stringForStatus(&status).c_str());
            usleep(1000000);
        }
    }
};

// This class implements the server side of the benchmark RPCs
class StressService : public basic::Basic::Service {   
public:
    grpc::Status Ping(grpc::ServerContext*context,
            const basic::Request *request,
            basic::Response *response) override
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
            grpc::ServerReader<basic::StreamOutRequest>* reader,
            basic::Response *response)
            override
    {
        lastOp = "StreamOut";
        basic::StreamOutRequest request;
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
            const basic::StreamInRequest *request,
            grpc::ServerWriter<basic::Response>* writer)
            override
    {
        lastOp = "StreamIn";
        basic::Response response;
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
            grpc::ServerReaderWriter<basic::Response,
            basic::StreamOutRequest>* stream)
            override
    {
        lastOp = "Stream2Way";
        basic::StreamOutRequest request;
        basic::Response response;
        while (stream->Read(&request)) {
            if (request.data_size() != request.requestitems()) {
                printf("Expected %d items in Stream2Way request, got %d\n",
                        request.requestitems(), request.data_size());
            }
            response.clear_data();
            int size = request.replyitems();
            if (size > 0) {
                for (int i = 0; i < size; i++) {
                    response.add_data(i);
                }
                stream->Write(response);
            }
            if (request.done()) {
                break;
            }
        }
        return grpc::Status::OK;
    }  
    
    grpc::Status PrintLog(grpc::ServerContext*context,
            const basic::Empty *request,
            basic::Empty *response) override
    {
        lastOp = "PrintLog";
        if (!logPrinted) {
            gpr_log(GPR_INFO, "Printing log because of remote request");
            printLog("stress.log");
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
    ClientStats *stats = &clientStats[id];
    int seed;
    std::mt19937 rng;

    if (getrandom(&seed, sizeof(seed), 0) != sizeof(seed)) {
        gpr_log(GPR_ERROR, "getrandom call failed");
    }
    gpr_log(GPR_INFO, "Client %d starting with random seed %d", id, seed);
    rng.seed(seed);
    
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
    
    int outBytes[numOps];
    int inBytes[numOps];
    outBytes[0] = 100;
    inBytes[0] = 100;
    outBytes[1] = 5000;
    inBytes[1] = 5000;
    outBytes[2] = 1200000;
    inBytes[2] = 1200000;
    outBytes[3] = sum(streamSizes);
    inBytes[3] = 5000;
    outBytes[4] = 0;
    inBytes[4] = sum(streamSizes);
    outBytes[5] = sum(stream2WayOutSizes);
    inBytes[5] = sum(stream2WayInSizes);
    
    while (true) {
        Target& target = targets[targetDist(rng)];
        int type = typeDist(rng);
        stats->ops[type]++;
        gpr_log(GPR_INFO, "Starting request, client %d type %d",
                id, type);
        uint64_t start = TimeTrace::rdtsc();
        switch (type) {
            // Issue a small ping request.
            case 0: {
                target.Ping(outBytes[0], inBytes[0]);
                break;
            }

            // Issue a medium-size ping request.
            case 1: {
                target.Ping(outBytes[1], inBytes[1]);
                break;
            }

            // Issue a large ping request.
            case 2: {
                target.Ping(outBytes[2], inBytes[2]);
                break;
            }

            // Stream data out to a server.
            case 3: {
                target.StreamOut(streamSizes, 5000);
                break;
            }

            // Stream data in from a server.
            case 4: {
                target.StreamIn(streamSizes);
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
        uint64_t elapsed = TimeTrace::rdtsc() - start;
        stats->outBytes[type] += outBytes[type];
        stats->inBytes[type] += inBytes[type];
        int i = stats->nextSample[type];
        stats->rtts[type][i] = elapsed;
        i++;
        if (i >= ClientStats::maxSamples) {
            i = 0;
        }
        stats->nextSample[type] = i;
    }
}

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
    printf("    --log-to-mem         Record gRPC log in memory (default: "
            "false)\n");
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

/**
 * Ask all of the servers to print their logs.
 */
void printAllLogs()
{
    for (Target& target: targets) {
        target.PrintLog();
    }
}

int main(int argc, char** argv)
{
    recordFunc = TimeTrace::record2;
    std::vector<std::string> args;
    unsigned nextArg;
    
    for (int i = 0; i < argc; i++) {
        args.emplace_back(argv[i]);
    }
    
    grpc_init();
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
        } else if (strcmp(option, "--log-to-mem") == 0) {
            gpr_set_log_function(logToMemory);
            gpr_set_log_verbosity(GPR_LOG_SEVERITY_INFO);
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
    
    // Number of consecutive iterations where each client appeared to
    // make no progress.
    int idle[numClientThreads];
    memset(idle, 0, sizeof(idle));
    
    // Each iteration through this loop prints statistics, then sleeps a while.
    while (true) {
        latestStats = 1-latestStats;
        ClientStats *curTotal = &totalStats[latestStats];
        ClientStats *oldTotal = &totalStats[1-latestStats];
        
        // These arrays build up strings listing rates of ops and data.
        std::string count;
        std::string outBytes;
        std::string inBytes;
        
        // RTT samples for current op.
        std::vector<double> rtts;
        
        double totalOps = 0.0;
        double totalOut = 0.0;
        double totalIn = 0.0;
        
        uint64_t currentTime = TimeTrace::rdtsc();
        double elapsed = TimeTrace::toSeconds(currentTime - lastSampleTime);
        
        // Gather and print current statistics.
        memset(curTotal, 0, sizeof(*curTotal));
        rtts.reserve(clientStats.size() * ClientStats::maxSamples);
        for (int op = 0; op < numOps; op++) {
            count.resize(0);
            outBytes.resize(0);
            inBytes.resize(0);
            rtts.resize(0);
            if (clientStats.size() == 0) {
                break;
            }
            for (size_t client = 0; client < clientStats.size(); client++) {
                ClientStats *cur = &clientStats[client];
                ClientStats *old = &prevClientStats[client];
                
                // First, check for a stuck client.
                if (op == 0) {
                    if (cur->ops[op] == old->ops[op]) {
                        idle[client]++;
                        printf("*** idle[%lu] is now %d\n", client,
                                idle[client]);
                        if (idle[client] == 5) {
                            printf("*** Printing log because client %lu "
                                    "is stuck\n", client);
                            gpr_log(GPR_ERROR, "Client %lu appears to be "
                                    "stuck!\n", client);
                            if (!logPrinted) {
                                printLog("stress.log");
                                printAllLogs();
                            }
                        }
                    } else {
                        idle[client] = 0;
                    }
                }
                
                double opsPerSec = (cur->ops[op] - old->ops[op])/elapsed;
                appendPrintf(count, " ", "%5.0f", opsPerSec);
                curTotal->ops[op] += cur->ops[op];
                old->ops[op] = cur->ops[op];

                double outPerSec = (cur->outBytes[op] - old->outBytes[op])
                        /elapsed;
                appendPrintf(outBytes, " ", "%5.2f", outPerSec*1e-6);
                curTotal->outBytes[op] += cur->outBytes[op];
                old->outBytes[op] = cur->outBytes[op];

                double inPerSec = (cur->inBytes[op] - old->inBytes[op])
                        /elapsed;
                appendPrintf(inBytes, " ", "%5.2f", inPerSec*1e-6);
                curTotal->inBytes[op] += cur->inBytes[op];
                old->inBytes[op] = cur->inBytes[op];
                
                uint64_t *current = clientStats[client].rtts[op];
                for (int count = ClientStats::maxSamples; count > 0; count--) {
                    if (*current == 0) {
                        break;
                    }
                    rtts.push_back(TimeTrace::toSeconds(*current));
                    current++;
                }
            }
            
            printf("%s:\n", opNames[op]);
            if (rtts.size() > 0) {
                std::sort(rtts.begin(), rtts.end());
                printf("  RTT:        min %5.0f us, P50 %5.0f us, P99 %5.0f us, "
                        "max %5.0f us\n",
                        rtts[0]*1e6, rtts[rtts.size()/2]*1e6,
                        rtts[rtts.size()*99/100]*1e6, rtts[rtts.size()-1]*1e6);
            }

            if (!firstTime) {
                double opsPerSec = (curTotal->ops[op]
                        - oldTotal->ops[op])/elapsed;
                totalOps += curTotal->ops[op] - oldTotal->ops[op];
                printf("  ops/sec:    %-6.0f [%s]\n", opsPerSec, count.c_str());
                double outPerSec = (curTotal->outBytes[op]
                        - oldTotal->outBytes[op])/elapsed;
                totalOut += curTotal->outBytes[op] - oldTotal->outBytes[op];
                printf("  MB/sec out: %-6.2f [%s]\n", outPerSec*1e-6,
                        outBytes.c_str());
                double inPerSec = (curTotal->inBytes[op]
                        - oldTotal->inBytes[op])/elapsed;
                totalIn += curTotal->inBytes[op] - oldTotal->inBytes[op];
                printf("  MB/sec in:  %-6.2f [%s]\n", inPerSec*1e-6,
                        inBytes.c_str());
            }
        }
        
        if (!firstTime && (clientStats.size() != 0)) {
            printf("Totals: %.0f ops/sec, %.2f MB/sec out %.2f MB/sec in\n",
                    totalOps/elapsed, 1e-06*totalOut/elapsed,
                    1e-06*totalOut/elapsed);
        }

        firstTime = false;
        lastSampleTime = currentTime;
        usleep(2000000);
    }
    
    exit(0);
}