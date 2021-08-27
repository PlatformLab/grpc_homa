#ifndef HOMA_LISTENER_H
#define HOMA_LISTENER_H

#include <mutex>
#include <optional>
#include <unordered_map>

#include <grpcpp/grpcpp.h>
#include "src/core/lib/surface/server.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/transport/transport_impl.h"

#include "homa_stream.h"
#include "rpc_id.h"
#include "wire.h"

/**
 * Stores all the state needed to serve Homa requests on a particular
 * port; owns the socket associated with that port. This structure also
 * serves as transport for all of the requests arriving via that port.
 */
class HomaListener : public grpc_core::Server::ListenerInterface {
public:
    void Orphan() override ;
    void Start(grpc_core::Server* server,
            const std::vector<grpc_pollset*>* pollsets) override;
    static HomaListener *Get(grpc_server* server, int port);
    void SetOnDestroyDone(grpc_closure* on_destroy_done) override;
    grpc_core::channelz::ListenSocketNode* channelz_listen_socket_node()
            const override;
    ~HomaListener();
    
protected:
	HomaListener(grpc_server* server, int port);
    static void     InitShared(void);
    static void     destroy(grpc_transport* gt);
    static void     destroy_stream(grpc_transport* gt, grpc_stream* gs,
                            grpc_closure* then_schedule_closure);
    static grpc_endpoint*
                    get_endpoint(grpc_transport* gt);
    static int      init_stream(grpc_transport* gt, grpc_stream* gs,
                            grpc_stream_refcount* refcount,
                            const void* init_info, grpc_core::Arena* arena);
    static void     onRead(void* arg, grpc_error* error);
    static void     perform_op(grpc_transport* gt, grpc_transport_op* op);
    static void     perform_stream_op(grpc_transport* gt, grpc_stream* gs,
                            grpc_transport_stream_op_batch* op);
    static void     set_pollset(grpc_transport* gt, grpc_stream* gs,
                            grpc_pollset* pollset);
    static void     set_pollset_set(grpc_transport* gt, grpc_stream* gs,
                            grpc_pollset_set* pollset_set);
    
    /**
     * Information that is shared across all HomaListener objects.
     */
    struct Shared {
        // Contains pointers to all open Homa ports: keys are port numbers.
        std::unordered_map<int, HomaListener *> ports;

        // Synchronizes access to this structure.
        std::mutex mutex;
        
        Shared() : ports(), mutex() {}
    };

    /**
     * This structure is used to pass data down through callbacks to
     * init_stream and back up again.
     */
    struct StreamInit {
        // Identifying information from incoming RPC.
        RpcId rpcId;

        // Homa's id for the incoming RPC.
        uint64_t homaId;

        // Used to return the HomaStream address back through callbacks.
        HomaStream *stream;
    };
    
    // Points to a virtual function table for use by the rest of gRPC to
    // treat this object as a transport. gRPC uses a pointer to this field
    // as a generic handle for the object. 
    grpc_transport transport;
    
    // @transport refers to this.
    struct grpc_transport_vtable vtable;
    
    // Associated gRPC server.
    grpc_core::OrphanablePtr<grpc_core::Server> server;
    
    // Keeps track of all RPCs currently in some stage of processing;
    // used to look up the Stream for an RPC based on its id.
    std::unordered_map<RpcId*, HomaStream*> activeRpcs;
    
    // Must be held when accessing @streams. Must not be acquired while
    // holding a stream lock.
    grpc_core::Mutex mutex;
    
    // Homa port number managed by this object.
    int port;
	
    // File descriptor for a Homa socket.
    int fd;
    
    // Used by grpc to manage the socket in various ways, such as epoll.
    grpc_fd *gfd;
    
    // Used to call us back when fd is readable.
    grpc_closure read_closure;
    
    // Used to notify gRPC of new incoming requests.
    void (*accept_stream_cb)(void* user_data, grpc_transport* transport,
                             const void* server_data);
    void* accept_stream_data;
    
    // Singleton object with common info.
    static std::optional<Shared> shared;
    
    // Used to synchronize initialization of shared.
    static gpr_once shared_once;
};

#endif // HOMA_LISTENER_H