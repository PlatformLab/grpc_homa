// Simple gRPC client for testing.

#include <netdb.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <cstdarg>

#include <grpcpp/grpcpp.h>
#include "test.grpc.pb.h"

#include "homa.h"
#include "homa_client.h"

class SumClient{
public:
    SumClient(const std::shared_ptr<grpc::Channel> channel)
        : stub(test::Test::NewStub(channel)) {}
        
    std::unique_ptr<test::Test::Stub> stub;
    
    /**
     * Adds two numbers together.
     * \param op1
     *      First number to add.
     * \param op2
     *      Second number to add.
     * \return
     *      Sum of the two numbers.
     */
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
    
    /**
     * Adds many numbers together.
     * \param op
     *      The first number to add.
     * \param ...
     *      A number of additional values, terminated by a -1 value.
     * \return
     *      The sum of all of the numbers (except the terminating one).
     */
    int SumMany(int op, ...) {
        test::SumResult result;
        grpc::ClientContext context;
        std::unique_ptr<grpc::ClientWriter<test::Value>> writer(
                stub->SumMany(&context, &result));
        va_list ap;
        va_start(ap, op);
        while (op != -1) {
            test::Value value;
            value.set_value(op);
            if (!writer->Write(value)) {
                break;
            }
            op = va_arg(ap, int);
        }
        writer->WritesDone();
        grpc::Status status = writer->Finish();
        if (!status.ok()) {
            printf("SumMany RPC failed: %s\n", status.error_message().c_str());
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
    int sum;
//    sum = client.Sum(22, 33);
//    printf("Sum of 22 and 33 is %d\n", sum);
    sum = client.SumMany(1, 2, 3, 4, 5, -1);
    printf("SumMany of 1..5 is %d\n", sum);
    return 0;
}