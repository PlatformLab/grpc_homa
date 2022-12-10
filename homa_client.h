#ifndef HOMA_CLIENT_H
#define HOMA_CLIENT_H

#include <list>
#include <mutex>
#include <unordered_map>

#include <grpcpp/grpcpp.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/connector.h"
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/transport/transport_impl.h"

#include "homa_socket.h"
#include "homa_stream.h"

/**
 * This structure stores all of the shared information for gRPC
 * clients using Homa. There is just one of these structures for
 * each application.
 */
class HomaClient {
public:
    static std::shared_ptr<grpc::Channel> createInsecureChannel(
            const char* target);

protected:
    HomaClient(bool ipv6);
    ~HomaClient();
    static void init();
    static grpc_channel *createChannel(const char* target,
            const grpc_channel_args* args);

    /**
     * This class provides credentials used to create Homa channels.
     */
    class InsecureCredentials final : public grpc::ChannelCredentials {
     public:
        std::shared_ptr<grpc::Channel> CreateChannelImpl(
                const std::string& target, const grpc::ChannelArguments& args)
                override;
        grpc::SecureChannelCredentials* AsSecureCredentials() override
        {
            return nullptr;
        }
    };

    /**
     * This class is invoked by gRPC to create subchannels for a channel.
     * There is typically only one instance.
     */
    class SubchannelFactory : public grpc_core::ClientChannelFactory {
    public:
        grpc_core::RefCountedPtr<grpc_core::Subchannel> CreateSubchannel(
            const grpc_resolved_address& address,
            const grpc_channel_args* args) override;
    };

    /**
     * An instance of this class creates "connections" for subchannels
     * of a given channel. It doesn't do much, since Homa doesn't have
     * connections.
     */
    class Connector : public grpc_core::SubchannelConnector {
    public:
        void Connect(const Args& args, Result* result, grpc_closure* notify)
            override;
        void Shutdown(grpc_error_handle error) override;
    };

    /**
     * An instance of this class contains information specific to a
     * peer. These objects are used as "transports" in gRPC, but unlike
     * transports for TCP, there's no network connection info here,
     * since Homa is connectionless.
     */
    struct Peer {
        // Contains a virtual function table for use by the rest of gRPC.
        // gRPC uses this as a transport handle.
        grpc_transport transport;

        // Shared client state.
        HomaClient *hc;

        // Linux struct sockaddr containing the IP address and port of the peer.
        grpc_resolved_address addr;

        Peer(HomaClient *hc, grpc_resolved_address addr);
    };

    static void     destroy(grpc_transport* gt);
    static void     destroy_stream(grpc_transport* gt, grpc_stream* gs,
                            grpc_closure* then_schedule_closure);
    static int      init_stream(grpc_transport* gt, grpc_stream* gs,
                            grpc_stream_refcount* refcount,
                            const void* server_data, grpc_core::Arena* arena);
    static void     onRead(void* arg, grpc_error* error);
    static void     perform_op(grpc_transport* gt, grpc_transport_op* op);
    static void     perform_stream_op(grpc_transport* gt, grpc_stream* gs,
                            grpc_transport_stream_op_batch* op);
    static void     set_pollset(grpc_transport* gt, grpc_stream* gs,
                            grpc_pollset* pollset);
    static void     set_pollset_set(grpc_transport* gt, grpc_stream* gs,
                            grpc_pollset_set* pollset_set);
    static grpc_endpoint*
                    get_endpoint(grpc_transport* gt);

    // Used by gRPC to invoke transport-specific functions on all
    // HomaPeer objects associated with this HomaClient.
    struct grpc_transport_vtable vtable;

    // Holds all streams with outstanding requests.
    std::unordered_map<StreamId, HomaStream*, StreamId::Hasher,
            StreamId::Pred> streams;

    // Id to use for the next outgoing RPC.
    int nextId;

    // Must be held when accessing @streams or @nextId. Must not be
    // acquired while holding a stream lock.
    grpc_core::Mutex mutex;

    // Holds information about the socket used for Homa communication,
    // such as information about receive buffers.
    HomaSocket sock;

    // Used to call us back when fd is readable.
    grpc_closure readClosure;

    // Number of peers that exist for this object.
    int numPeers;

    // Single shared HomaClient used for all channels.  Nullptr means
    // not created yet.
    static HomaClient *sharedClient;

    // Held when creating or deleting sharedClient and when updating numPeers.
    static grpc_core::Mutex refCountMutex;

    // Used to create subchannels for all Homa channels.
    static SubchannelFactory factory;
};

#endif // HOMA_CLIENT_H
