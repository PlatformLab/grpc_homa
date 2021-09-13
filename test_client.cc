// Simple gRPC client for testing.

#include <netdb.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <grpcpp/grpcpp.h>
#include "test.grpc.pb.h"

#include "homa.h"
#include "homa_client.h"

class SumClient{
public:
    SumClient(const std::shared_ptr<grpc::Channel> channel)
        : stub(test::Test::NewStub(channel)) {}
        
    std::unique_ptr<test::Test::Stub> stub;
    
    int Sum(int op1, int op2)
    {
        test::SumArgs args;
        test::SumResult result;
        grpc::ClientContext context;
        
        args.set_op1(op1);
        args.set_op2(op2);
        grpc::Status status = stub->Sum(&context, args, &result);
        if (!status.ok()) {
            printf("Sum RPC failed: %s\n", status.error_message().c_str());
            return -1;
        }
        return result.sum();
    }
};

int main(int argc, char** argv) {
    const char *server;
    
    if (argc == 1) {
        server = "node-1:4000";
    } else if (argc == 2) {
        server = argv[1];
    } else {
        fprintf(stderr, "Usage: test_client [host:port]\n");
        return 1;
    }
    SumClient client(HomaClient::createInsecureChannel(server));
    int sum = client.Sum(22, 33);
    printf("Sum of 22 and 33 is %d\n", sum);
    return 0;
}