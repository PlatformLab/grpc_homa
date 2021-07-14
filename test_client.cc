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
            printf("Sum RPC failed!\n");
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
                HomaTransport::create_channel(target.c_str(), &channel_args),
                std::move(interceptor_creators));
    }

    grpc::SecureChannelCredentials* AsSecureCredentials() override {
        return nullptr;
    }
};

int main(int argc, char** argv) {
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
#if 0
    struct sockaddr_in dest;
    uint64_t rpc_id;
    int status;
    dest.sin_addr.s_addr = htonl((127<<24) + 1);
    dest.sin_family = AF_INET;
    dest.sin_port = htons(4000);
    printf("IP address is 0x%x\n", dest.sin_addr.s_addr);
    
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_HOMA);
	if (fd < 0) {
		fprintf(stderr, "Couldn't open Homa socket: %s\n", strerror(errno));
		exit(1);
	}
    char buffer[100];
    buffer[0] = 'a';
    status = homa_send(fd, buffer, sizeof(buffer),
            reinterpret_cast<struct sockaddr *>(&dest), sizeof(dest), &rpc_id);
    if (status < 0) {
        fprintf(stderr, "Error in homa_send: %s\n", strerror(errno));
        exit(1);
    }
    printf("Sent %lu bytes to server\n", sizeof(buffer));
    exit(0);
#endif
    
//    SumClient client(grpc::CreateChannel("localhost:50051",
//            grpc::InsecureChannelCredentials()));
    std::shared_ptr<InsecureHomaCredentials> creds(new InsecureHomaCredentials());
    SumClient client(grpc::CreateChannel("node-1:4000", creds));
    int sum = client.Sum(22, 33);
    printf("Sum of 22 and 33 is %d\n", sum);
    return 0;
}