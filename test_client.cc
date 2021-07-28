// Simple gRPC client for testing.

#include <netdb.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <grpcpp/grpcpp.h>
#include "test.grpc.pb.h"

#include "homa.h"
#include "homa_transport.h"

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

class InsecureHomaCredentials final : public grpc::ChannelCredentials {
 public:
    std::shared_ptr<grpc::Channel> CreateChannelImpl(
            const std::string& target, const grpc::ChannelArguments& args)
            override {
        return CreateChannelWithInterceptors(target, args,
                std::vector<std::unique_ptr<
                grpc::experimental::ClientInterceptorFactoryInterface>>());
    }

    std::shared_ptr<grpc::Channel> CreateChannelWithInterceptors(
            const std::string& target, const grpc:: ChannelArguments& args,
            std::vector<std::unique_ptr<
                    grpc::experimental::ClientInterceptorFactoryInterface>>
                    interceptor_creators) override {
        grpc_channel_args channel_args;
        args.SetChannelArgs(&channel_args);
        return ::grpc::CreateChannelInternal("",
                HomaClient::create_channel(target.c_str(), &channel_args),
                std::move(interceptor_creators));
    }

    grpc::SecureChannelCredentials* AsSecureCredentials() override {
        return nullptr;
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
//    struct addrinfo hints;
//    struct addrinfo *matching_addresses;
    
//    memset(&hints, 0, sizeof(struct addrinfo));
//    hints.ai_family = AF_INET;
//    hints.ai_socktype = SOCK_DGRAM;
//    status = getaddrinfo("localhost:4000", NULL, &hints,
//            &matching_addresses);
//    if (status != 0) {
//        fprintf(stderr, "Couldn't look up address for localhost:4000: %s\n",
//                gai_strerror(status));
//        exit(1);
//    }
//    dest = matching_addresses->ai_addr;
    
//    SumClient client(grpc::CreateChannel("localhost:50051",
//            grpc::InsecureChannelCredentials()));
    std::shared_ptr<InsecureHomaCredentials> creds(new InsecureHomaCredentials());
    SumClient client(grpc::CreateChannel(server, creds));
    int sum = client.Sum(22, 33);
    printf("Sum of 22 and 33 is %d\n", sum);
    return 0;
}