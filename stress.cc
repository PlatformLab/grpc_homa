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

// This class provides client-side entry points for the test RPCs.]
class StressClient {
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
    StressClient(int serverIndex, int serverPort)
        : stub()
        , server()
    {
        snprintf(server, sizeof(server), "node-%d:%d",
                serverIndex, serverPort);
        stub = stress::Stress::NewStub(
                HomaClient::createInsecureChannel(server));
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

// Each element of this is used to access one server:
std::vector<StressClient> channels;

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
 * The top-level method for each client thread.
 * \param id
 *      Index of this thread among all client threads.
 */
void client(int id)
{
    std::mt19937 rng;
    std::uniform_int_distribution<int> channelDist(0, channels.size()-1);
    std::uniform_int_distribution<int> opDist(0, 5);
    
    std::vector<int> streamOutSizes = makeVector<int>(
            5, 100, 5000, 1200000, 400, 50000);
    
    std::vector<int> stream2WayOutSizes = makeVector<int>(
            7, 100, 200, 1200000, 3000, 10000, 400, 50000);
    std::vector<int> stream2WayInSizes = makeVector<int>(
            7, 0, 5000, 300, 9000, 0, 0, 5000);
    
    rng.seed(id);
    while (true) {
        int channel = channelDist(rng);
        channels[channel].Ping(100, 100);
        printf("Ping request completed for client %d, server %s\n",
                id, channels[channel].server);
        sleep(1);
        channels[channel].StreamOut(streamOutSizes, 5000);
        printf("StreamOut request completed for client %d, server %s\n",
                id, channels[channel].server);
        sleep(1);
        channels[channel].StreamIn(streamOutSizes);
        printf("StreamIn request completed for client %d, server %s\n",
                id, channels[channel].server);
        sleep(1);
        channels[channel].Stream2Way(stream2WayOutSizes, stream2WayInSizes);
        printf("Stream2Way request completed for client %d, server %s\n",
                id, channels[channel].server);
        sleep(1);
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
    printf("    --server-ports       How many ports will be open on each "
            "server for\n");
    printf("                         receiving requests (default: 1)\n");
    printf("    --servers            How many nodes will be acting as servers "
            "(default: 1)\n");
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
            
            snprintf(address, sizeof(address), "homa:%d", 4000+i);
            builder.AddListeningPort(address,
                    HomaListener::insecureCredentials());
        }
        builder.RegisterService(&service);
        server = builder.BuildAndStart();
        if (server == nullptr) {
            exit(1);
        }
    }
    
    for (int i = 0; i < numServers; i++) {
        for (int j = 0; j < numServerPorts; j++) {
            channels.emplace_back(firstServer + i, 4000+j);
        }
    }
    
    for (int i = 0; i < numClientThreads; i++) {
        std::thread t(client, i);
        t.detach();
    }
    
    while (true) {
        usleep(1000000);
    }
    
    exit(0);
}