// gRPC server for testing.

#include <grpc/grpc.h>
#include <grpcpp/grpcpp.h>

#include "test.grpc.pb.h"
#include "homa.h"
#include "homa_listener.h"

class TestImpl : public test::Test::Service {   
public:    
    grpc::Status Sum(grpc::ServerContext*context, const test::SumArgs *args,
            test::SumResult *result) {
        printf("Sum invoked with arguments %d and %d\n",
                args->op1(), args->op2());
        result->set_sum(args->op1() + args->op2());
        return grpc::Status::OK;
    }
    
    grpc::Status SumMany(grpc::ServerContext* context,
            grpc::ServerReader<test::Value>* reader, test::SumResult *result)
    {
        test::Value value;
        int sum = 0;
        while (reader->Read(&value)) {
            printf("SumMany received value %d\n", value.value());
            sum += value.value();
        }
        result->set_sum(sum);
        printf("Returning result: %d\n", sum);
        return grpc::Status::OK;
    }
};

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s homa:port\n", argv[0]);
        exit(1);
    }
    printf("sizeof(struct sockaddr_in6): %lu\n", sizeof(struct sockaddr_in6));
    std::string server_address(argv[1]);
    TestImpl service;

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address,
            HomaListener::insecureCredentials());
  //  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    if (server == nullptr)
        exit(1);
    std::cout << "Server listening on " << server_address << std::endl;
    server->Wait();

    return 0;
}