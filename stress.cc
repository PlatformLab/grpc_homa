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
class StressClient{
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
     * Send a variable-size buffer, 
     * \param requestLength
     *      Number of bytes in buffer to send (will be rounded down to
     *      multiple of 4).
     * \param responseLength
     *      Number of bytes in response buffer (will be rounded down to
     *      multiple of 4).
     */
    void Ping(int requestLength, int replyLength)
    {
        stress::PingRequest request;
        stress::PingResponse result;
        grpc::ClientContext context;
        
        request.set_replylength(replyLength);
        for (int i = 0; i < requestLength>>2; i++) {
            request.add_data(i);
        }
        grpc::Status status = stub->Ping(&context, request, &result);
        if (!status.ok()) {
            printf("Ping RPC to %s failed: %s\n",
                    server, stringForStatus(&status).c_str());
            usleep(1000000);
        }
    }
};

// This class implements the server side of the benchmark RPCs
class StressService : public stress::Stress::Service {   
public:    
    grpc::Status Ping(grpc::ServerContext*context,
            const stress::PingRequest *request,
            stress::PingResponse *response) override
    {
        int length = request->replylength();
        for (int i = 0; i < length; i++) {
            response->add_data(i);
        }
        return grpc::Status::OK;
    }
};

// Each element of this is used to access one server:
std::vector<StressClient> channels;

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
    
    rng.seed(id);
    while (true) {
        int channel = channelDist(rng);
        channels[channel].Ping(100, 100);
        printf("Ping request succeeded for client %d, server %s\n",
                id, channels[channel].server);
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
            "argument, default is false)");
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