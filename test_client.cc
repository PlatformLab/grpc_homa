// Simple gRPC client for testing.

#include <netdb.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <grpcpp/grpcpp.h>
#include "test.grpc.pb.h"

#include "homa.h"

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
//    struct addrinfo hints;
//    struct addrinfo *matching_addresses;
    struct sockaddr_in dest;
    uint64_t rpc_id;
    int status;
    
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
    
    
    SumClient client(grpc::CreateChannel("localhost:50051",
            grpc::InsecureChannelCredentials()));
    int sum = client.Sum(22, 33);
    printf("Sum of 22 and 33 is %d\n", sum);
    return 0;
}