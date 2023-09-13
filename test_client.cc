// Simple gRPC client for testing.

#include <netdb.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <cstdarg>
#include <optional>

#include <grpcpp/grpcpp.h>
#include "test.grpc.pb.h"

#include "homa.h"
#include "homa_client.h"
#include "time_trace.h"
#include "util.h"

const char *ttFile = "homa.tt";
const char *ttServerFile = "homaServer.tt";

class TestClient{
public:
    TestClient(const std::shared_ptr<grpc::Channel> channel)
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

        context.AddMetadata("md1", "md1_value");
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
     * Issues 3 concurrent asynchronous Sum RPCs, waits for all results,
     * and prints results.
     */
    void Sum3Async()
    {
        test::SumArgs args[3];
        test::SumResult results[3];
        grpc::ClientContext contexts[3];
        grpc::CompletionQueue cq;
        grpc::Status statuses[3];

        args[0].set_op1(100);
        args[0].set_op2(200);
        args[1].set_op1(300);
        args[1].set_op2(400);
        args[2].set_op1(500);
        args[2].set_op2(600);

        std::unique_ptr<grpc::ClientAsyncResponseReader<test::SumResult>> rpc0(
                stub->AsyncSum(&contexts[0], args[0], &cq));
        std::unique_ptr<grpc::ClientAsyncResponseReader<test::SumResult>> rpc1(
                stub->AsyncSum(&contexts[1], args[1], &cq));
        std::unique_ptr<grpc::ClientAsyncResponseReader<test::SumResult>> rpc2(
                stub->AsyncSum(&contexts[2], args[2], &cq));

        rpc0->Finish(&results[0], &statuses[0], (void *) 1);
        rpc1->Finish(&results[1], &statuses[1], (void *) 2);
        rpc2->Finish(&results[2], &statuses[2], (void *) 3);

        for (int i = 0; i < 3; i++) {
            uint64_t got_tag;
            bool ok = false;

            if (!cq.Next(reinterpret_cast<void **>(&got_tag), &ok) || !ok) {
                printf("Sum3Async failed: couldn't get event from "
                        "completion queue\n");
                return;
            }

            if ((got_tag < 1) || (got_tag > 3)) {
                printf("Sum3Async received bad tag %lu\n", got_tag);
                return;
            }

            printf("Sum3Async operation %lu completed with result %d\n",
                    got_tag, results[got_tag-1].sum());
        }
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
     * with incremented value. Repeats this many times.
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
    /**
     * Have the peer print its timetrace to a file.
     * \param name
     *      Name of the file in which to print the trace.
     */
    void PrintTrace(const char *name)
    {
        test::String args;
        test::Empty result;
        grpc::ClientContext context;

        args.set_s(name);
        grpc::Status status = stub->PrintTrace(&context, args, &result);
        if (!status.ok()) {
            printf("PrintTrace RPC failed: %s\n",
                    stringForStatus(&status).c_str());
        }
    }
};

void measureRtt(TestClient *client)
{
#define NUM_REQUESTS 50000
    for (int i = 0; i < 5; i++) {
        client->Sum(1, 2);
    }
    uint64_t start = TimeTrace::rdtsc();
    for (int i = 0; i < NUM_REQUESTS; i++) {
        tt("Starting request");
        client->Sum(1, 2);
        tt("Request %d finished", i);
        tt("TimeTrace::record completed");
        tt("Second TimeTrace::record completed");
    }
    uint64_t end = TimeTrace::rdtsc();
    printf("Round-trip time for Sum requests: %.1f us\n",
            TimeTrace::toSeconds(end-start)*1e06/NUM_REQUESTS);
    client->PrintTrace(ttServerFile);
    TimeTrace::printToFile(ttFile);
}

int main(int argc, char** argv)
{
    const char *server = "node1:4000";
    bool useHoma = true;
    std::vector<std::string> args;
    unsigned nextArg;

    for (int i = 0; i < argc; i++) {
        args.emplace_back(argv[i]);
    }

    for (nextArg = 1; nextArg < args.size(); nextArg++) {
		const char *option = args[nextArg].c_str();
        if (strcmp(option, "--tcp") == 0) {
            useHoma = false;
            continue;
        }
        break;
    }
    if (nextArg != args.size()) {
        if (nextArg != (args.size() - 1)) {
            fprintf(stderr, "Usage: test_client [--tcp] [host::port]\n");
            exit(1);
        }
        server = args.back().c_str();
    }

    std::optional<TestClient> client;
    if (useHoma) {
        client.emplace(HomaClient::createInsecureChannel(server));
    } else {
        ttFile = "tcp.tt";
        ttServerFile = "tcpServer.tt";
        client.emplace(grpc::CreateChannel(server,
                grpc::InsecureChannelCredentials()));
    }
    int sum;
    sum = client->Sum(22, 33);
    printf("Sum of 22 and 33 is %d\n", sum);
//    client->Sum3Async();
//    printf("SumMany of 1..5 is %d\n", client->SumMany(1, 2, 3, 4, 5, -1));
//    client->PrintValues(21);
//    client->IncMany(3, 4);
//    measureRtt(&client.value());

    return 0;
}