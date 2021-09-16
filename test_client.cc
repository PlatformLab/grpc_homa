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
#include "util.h"

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
            printf("Sum RPC failed: %s\n", stringForStatus(&status).c_str());
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
            printf("SumMany RPC failed: %s\n", stringForStatus(&status).c_str());
            return -1;
        }
        return result.sum();
    }
    
    /**
     * Sends a seed value to the server, then prints all the individual
     * values that the server returns.
     * \param seed
     *      Server will use this value to determine how many values to
     *      return.
     */
    void PrintValues(int seed)
    {
        grpc::ClientContext context;
        test::Value value;
        test::Value response;
        value.set_value(seed);
        int numResponses = 0;
        
        std::unique_ptr<grpc::ClientReader<test::Value> > reader(
                stub->GetValues(&context, value));
        while (reader->Read(&response)) {
            printf("PrintValues received value %d\n", response.value());
            numResponses++;
        }
        printf("PrintValues received %d response messages\n", numResponses);
        grpc::Status status = reader->Finish();
        if (!status.ok()) {
            printf("PrintValues RPC failed: %s\n",
                    stringForStatus(&status).c_str());
        }
    }
    
    /**
     * Sends an initial value to the server, checks to see that the server responds
     * with prevented value. Repeats this many times.
     * \param initial
     *      Additional value to send to the server.
     * \param count
     *      How many times to have the server increment the value.
     */
    void IncMany(int initial, int count)
    {
        int current = initial;
        grpc::ClientContext context;
        std::shared_ptr<grpc::ClientReaderWriter<test::Value, test::Value>>
                stream(stub->IncMany(&context));
        test::Value request;
        test::Value response;
        
        for (int i = 0; i < count; i++) {
            request.set_value(current);
            if (!stream->Write(request)) {
                break;
            }
            if (!stream->Read(&response)) {
                break;
            }
            printf("IncMany sent %d, got %d\n", current, response.value());
            current = response.value();
        }
        stream->WritesDone();
        grpc::Status status = stream->Finish();
        if (!status.ok()) {
            printf("IncMany RPC failed: %s\n",
                    stringForStatus(&status).c_str());
        }
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
//    int sum;
//    sum = client.Sum(22, 33);
//    printf("Sum of 22 and 33 is %d\n", sum);
    printf("SumMany of 1..5 is %d\n", client.SumMany(1, 2, 3, 4, 5, -1));
//    client.PrintValues(21);
    client.IncMany(3, 4);
    return 0;
}