// This file contains both the client and server code for a simple
// gRPC application.
//
// Usage:
// tcp_test server port
// tcp test client port@host

#include <string>

#include <grpc/grpc.h>
#include <grpcpp/grpcpp.h>

#include "test.grpc.pb.h"

// Server-side code to implement the service.
class SumService : public test::Test::Service {   
public:    
    grpc::Status Sum(grpc::ServerContext*context, const test::SumArgs *args,
            test::SumResult *result) {
        result->set_sum(args->op1() + args->op2());
        return grpc::Status::OK;
    }
};

// Client-side wrapper class.
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
            printf("Sum RPC failed!\n");
            return -1;
        }
        return result.sum();
    }
};

int main(int argc, char** argv) {
    if (argc < 2) {
        goto usage;
    }
    if (strcmp(argv[1], "client") == 0) {
        std::string target = "node-1:4000";
        if (argc == 3) {
            target = argv[2];
        } else if (argc != 2) {
            goto usage;
        }
        SumClient client(grpc::CreateChannel(target,
            grpc::InsecureChannelCredentials()));
        int sum = client.Sum(22, 33);
        printf("Sum of 22 and 33 is %d\n", sum);
        return 0;
    } else if (strcmp(argv[1], "server") == 0) {
        std::string port = "0.0.0.0:4000";
        if (argc == 3) {
            port = "0.0.0.0:";
            port += argv[2];
        } else if (argc != 2) {
            goto usage;
        }
        SumService service;
        grpc::ServerBuilder builder;
        builder.AddListeningPort(port, grpc::InsecureServerCredentials());
        builder.RegisterService(&service);
        std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
        if (server == nullptr)
            exit(1);
        server->Wait();
    } else {
        goto usage;
    }
    exit(0);

usage:
    fprintf(stderr, "Usage: %s client server:port or %s server port\n",
            argv[0], argv[0]);
    exit(1);
}