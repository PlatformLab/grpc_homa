// gRPC server for testing.

#include <grpc/grpc.h>
#include <grpcpp/grpcpp.h>

#include "test.grpc.pb.h"
#include "homa.h"
#include "homa_listener.h"
#include "time_trace.h"
#include "util.h"

class TestImpl : public test::Test::Service {
public:
    grpc::Status Sum(grpc::ServerContext* context, const test::SumArgs *args,
            test::SumResult *result) override
    {
//        printf("Sum invoked with arguments %d and %d\n",
//                args->op1(), args->op2());
        tt("Sum service method invoked");
//        printf("%lu metadata values from client\n",
//                context->client_metadata().size());
//        for (auto md: context->client_metadata()) {
//            printf("Incoming metadata: name '%.*s', value '%.*s'\n",
//                    static_cast<int>(md.first.length()), md.first.data(),
//                    static_cast<int>(md.second.length()), md.second.data());
//        }
        result->set_sum(args->op1() + args->op2());
        return grpc::Status::OK;
    }

    grpc::Status SumMany(grpc::ServerContext* context,
            grpc::ServerReader<test::Value>* reader, test::SumResult *result)
            override
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

    grpc::Status GetValues(grpc::ServerContext* context, const test::Value *arg,
            grpc::ServerWriter<test::Value>* writer) override
    {
        test::Value response;
        for (int i = 1; i <= arg->value(); i += 2) {
            response.set_value(i);
            printf("Writing response with %d\n", i);
            writer->Write(response);
        }
        printf("GetValues finished (input %d)\n", arg->value());
        return grpc::Status::OK;
    }

    grpc::Status IncMany(grpc::ServerContext* context,
            grpc::ServerReaderWriter<test::Value, test::Value>* stream) override
    {
        test::Value request;
        test::Value response;
        while (stream->Read(&request)) {
            printf("IncMany got value %d\n", request.value());
            response.set_value(request.value() + 1);
            stream->Write(response);
        }
        printf("IncMany finished\n");
        return grpc::Status::OK;
    }

    grpc::Status PrintTrace(grpc::ServerContext*context,
            const test::String *args, test::Empty *result) override
    {
        TimeTrace::printToFile(args->s().c_str());
        return grpc::Status::OK;
    }
};

int main(int argc, char** argv) {
    recordFunc = TimeTrace::record2;
    std::string serverAddress;
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
            fprintf(stderr, "Usage: test_server [--tcp] [homa:port]\n");
            exit(1);
        }
        serverAddress = args.back();
    }
    if (serverAddress.empty()) {
        if (useHoma) {
            serverAddress = "homa:4000";
        } else {
            serverAddress = "0.0.0.0:4000";
        }
    }


    TestImpl service;
    grpc::ServerBuilder builder;
    if (useHoma) {
        builder.AddListeningPort(serverAddress,
                HomaListener::insecureCredentials());
    } else {
        builder.AddListeningPort(serverAddress, grpc::InsecureServerCredentials());
    }
    builder.RegisterService(&service);
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    if (server == nullptr)
        exit(1);
    std::cout << "Server listening on " << serverAddress << std::endl;
    server->Wait();

    return 0;
}