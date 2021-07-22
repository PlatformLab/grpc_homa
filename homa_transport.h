#include <list>
#include <mutex>

#include <grpcpp/grpcpp.h>

//#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/connector.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/transport/transport_impl.h"

/**
 * This structure stores all of the shared information for gRPC
 * clients using Homa. There is just one of these structures for
 * each application.
 */
class HomaClient {
public:
    static grpc_channel *create_channel(const char* target,
            const grpc_channel_args* args);
    
protected:
    HomaClient();
    ~HomaClient();
    static void init();
    

    /**
     * This class is invoked by gRPC to create subchannels for a channel.
     * There is typically only one instance.
     */
    class SubchannelFactory : public grpc_core::ClientChannelFactory {
    public:
        grpc_core::RefCountedPtr<grpc_core::Subchannel> CreateSubchannel(
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
        // Must be the first member variable!!
        grpc_transport base;

        // Shared client state.
        HomaClient *hc;

        // Linux struct sockaddr containing the IP address and port of the peer.
        grpc_resolved_address addr;

        Peer(HomaClient *hc, grpc_resolved_address *addr);
    };
    
    /**
     * This structure holds the state for a single RPC.
     */
    struct Stream {
        Stream(HomaClient *ht, grpc_stream_refcount* refs,
                grpc_core::Arena* arena)
            : hc(ht), refs(refs), arena(arena)
        { }
        
        // Transport that the stream belongs to.
        HomaClient *hc;
        
        // Reference count (owned externally).
        grpc_stream_refcount* refs;
        
        // Don't yet know what this is for (memory allocation?)
        grpc_core::Arena* arena;
    };
    
    static int      init_stream(grpc_transport* gt, grpc_stream* gs,
                            grpc_stream_refcount* refcount,
                            const void* server_data, grpc_core::Arena* arena);
    static void     set_pollset(grpc_transport* gt, grpc_stream* gs,
                            grpc_pollset* pollset);
    static void     set_pollset_set(grpc_transport* gt, grpc_stream* gs,
                            grpc_pollset_set* pollset_set);
    static void     perform_stream_op(grpc_transport* gt, grpc_stream* gs,
                            grpc_transport_stream_op_batch* op);
    static void     perform_op(grpc_transport* gt, grpc_transport_op* op);
    static void     destroy_stream(grpc_transport* gt, grpc_stream* gs,
                            grpc_closure* then_schedule_closure);
    static void     destroy(grpc_transport* gt);
    static grpc_endpoint*
                    get_endpoint(grpc_transport* gt);
    
    // Used by gRPC to invoke transport-specific functions on all
    // HomaPeer objects associated with this HomaClient.
    struct grpc_transport_vtable vtable;
    
    // Holds all existing streams owned by this transport.
    std::list<Stream *> streams;
    
    // File descriptor for Homa socket; used for all outgoing RPCs.
    // < 0 means socket couldn't be opened.
    int fd;
    
    // Single shared transport used for all channels.  Nullptr means
    // not created yet.
    static HomaClient *sharedClient;
    
    // Used to create subchannels for all Homa channels.
    static SubchannelFactory* factory;
};
