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

#include "homa_socket.h"
#include "homa_stream.h"
#include "wire.h"

#ifndef __UNIT_TEST__
#define PROTECTED protected
#else
#define PROTECTED public
#endif

/**
 * Stores all the state needed to serve Homa requests on a particular
 * port.
 */
class HomaListener : public grpc_core::Server::ListenerInterface {
public:
    static HomaListener *Get(grpc_server* server, int *port, bool ipv6);
    static std::shared_ptr<grpc::ServerCredentials> insecureCredentials(void);

PROTECTED:
    /**
     * This class provides credentials to create Homa listeners.
     */
    class InsecureCredentials final : public grpc::ServerCredentials {
    public:
         int AddPortToServer(const std::string& addr, grpc_server* server);
         void SetAuthMetadataProcessor(
                  const std::shared_ptr<grpc::AuthMetadataProcessor>& processor)
                 override {
             (void)processor;
             GPR_ASSERT(0);  // Should not be called on insecure credentials.
         }
    };

    /**
     * This class manages the Homa socket associated with the listener, and
     * contains most of the listener functionality. In the TCP world, the
     * listener corresponds to the listen socket and the transport corresponds
     * to all of the individual data connections; in the Homa world, a single
     * Homa socket serves both purposes, and it is managed here in the
     * transport. Thus there isn't much left in the listener.
     */
    class Transport {
    public:
        Transport(grpc_server* server, int *port, bool ipv6);
        ~Transport();
        HomaStream *    getStream(HomaIncoming *msg,
                                std::optional<grpc_core::MutexLock>& streamLock);
        void            shutdown();
        void            start(grpc_core::Server* server,
                                const std::vector<grpc_pollset*>* pollsets);
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

    PROTECTED:
        /**
         * This structure is used to pass data down through callbacks to
         * init_stream and back up again.
         */
        struct StreamInit {
            // Identifying information from incoming RPC.
            StreamId *streamId;

            // Used to return the HomaStream address back through callbacks.
            HomaStream *stream;
        };

        // Points to a virtual function table for use by the rest of gRPC to
        // treat this object as a transport. gRPC uses a pointer to this field
        // as a generic handle for the object.
        grpc_transport vtable;

        // Associated gRPC server. Not owned by this object
        grpc_core::Server *server;

        // Keeps track of all RPCs currently in some stage of processing;
        // used to look up the Stream for an RPC based on its id.
        std::unordered_map<StreamId, HomaStream*, StreamId::Hasher,
                StreamId::Pred> activeRpcs;

        typedef std::unordered_map<StreamId, HomaStream*,
                StreamId::Hasher, StreamId::Pred>::iterator ActiveIterator;

        // Must be held when accessing @activeRpcs. Must not be acquired while
        // holding a stream lock.
        grpc_core::Mutex mutex;
        
        // Manages the Homa socket, including buffer space.
        HomaSocket sock;

        // Used to call us back when fd is readable.
        grpc_closure read_closure;

        grpc_core::ConnectivityStateTracker state_tracker;

        // Used to notify gRPC of new incoming requests.
        void (*accept_stream_cb)(void* user_data, grpc_transport* transport,
                const void* server_data);
        void* accept_stream_data;

        friend class TestListener;
    };

    HomaListener(grpc_server* server, int *port, bool ipv6);
    ~HomaListener();
    void Orphan() override ;
    void SetOnDestroyDone(grpc_closure* on_destroy_done) override;
    void Start(grpc_core::Server* server,
            const std::vector<grpc_pollset*>* pollsets) override;
    grpc_core::channelz::ListenSocketNode* channelz_listen_socket_node()
            const override;

    // Transport associated with the listener; its lifetime is managed
    // outside this class.
    Transport *transport;

    // Homa port number associated with this listener.
    int port;

    grpc_closure* on_destroy_done;

    /**
     * Information that is shared across all HomaListener/Transport objects.
     */
    struct Shared {
        // Contains pointers to all open Homa ports: keys are port numbers.
        std::unordered_map<int, HomaListener *> ports;

        // Synchronizes access to this structure.
        grpc_core::Mutex mutex;

        // Function table shared across all HomaListeners.
        struct grpc_transport_vtable vtable;
        Shared() : ports(), mutex() {}
    };
    // Singleton object with common info.

    static std::optional<Shared> shared;
    static gpr_once shared_once;
    static void     InitShared(void);

    friend class TestListener;
};

#endif // HOMA_LISTENER_H
