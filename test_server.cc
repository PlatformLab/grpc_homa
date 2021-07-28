// gRPC server for testing.

#include <grpc/grpc.h>
#include <grpcpp/grpcpp.h>

#include "test.grpc.pb.h"
#include "homa.h"
#include "homa_listener.h"

class InsecureHomaCredentials final : public grpc::ServerCredentials {
 public:
    int AddPortToServer(const std::string& addr, grpc_server* server);
    void SetAuthMetadataProcessor(
            const std::shared_ptr<grpc::AuthMetadataProcessor>& processor)
            override {
        (void)processor;
        GPR_ASSERT(0);  // Should not be called on insecure credentials.
    }
};

int InsecureHomaCredentials::AddPortToServer(const std::string& addr,
        grpc_server* server)
{
    int port;
    char* end;
    
    if (strncmp("homa:", addr.c_str(), 5) != 0) {
        gpr_log(GPR_ERROR, "bad Homa port specifier '%s', must be 'homa:port'",
                addr.c_str());
        return 0;
    };
    port = strtol(addr.c_str()+5, &end, 0);
    if ((*end != '\0') || (port <= 0)) {
        gpr_log(GPR_ERROR, "bad Homa port number '%s', must be between 1 and %d",
                addr.c_str()+5, HOMA_MIN_CLIENT_PORT-1);
        return 0;
    }
    HomaListener *listener = HomaListener::Get(server, port);
    if (listener) {
        server->core_server->AddListener(grpc_core::OrphanablePtr
                <grpc_core::Server::ListenerInterface>(listener));
    }
    return 1;
}

class TestImpl : public test::Test::Service {   
public:    
    grpc::Status Sum(grpc::ServerContext*context, const test::SumArgs *args,
            test::SumResult *result) {
        result->set_sum(args->op1() + args->op2());
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
    std::shared_ptr<InsecureHomaCredentials> creds(new InsecureHomaCredentials());

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, creds);
  //  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    if (server == nullptr)
        exit(1);
    std::cout << "Server listening on " << server_address << std::endl;
    server->Wait();

    return 0;
}