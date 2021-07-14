#include <list>
#include <mutex>

#include <grpcpp/grpcpp.h>

//#include <grpc/support/port_platform.h>

#include "src/core/lib/transport/transport_impl.h"

//using grpc::grpc_transport, grpc::grpc_stream, grpc::grpc_stream_refcount;

/**
 * Allows gRPC messages to be sent using the Homa transport protocol.
 */
class HomaTransport {
public:
    static grpc_channel *create_channel(const char* target,
            const grpc_channel_args* args);
    
protected:
    HomaTransport();
    ~HomaTransport();
    /**
     * Represents a byte stream for communication with a particular server;
     * this has dubious value for Homa, since Homa is RPC-based, not
     * stream-based.
     */
    struct Stream {
        Stream(HomaTransport *ht, grpc_stream_refcount* refs,
                grpc_core::Arena* arena)
            : ht(ht), refs(refs), arena(arena)
        { }
        
        // Transport that the stream belongs to.
        HomaTransport *ht;
        
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
	
    // Contains a virtual function table for use by the rest of gRPC.
    // Must be the first member variable!!
    grpc_transport base;
    
    // Used by gRPC to invoke transport-specific functions.
    struct grpc_transport_vtable vtable;
    
    // Holds all existing streams owned by this transport.
    std::list<Stream *> streams;
    
    // File descriptor for Homa socket; used for all outgoing RPCs.
    // < 0 means socket couldn't be opened.
    int fd;
    
    // Single shared transport used for all channels.  Nullptr means
    // not created yet.
    static HomaTransport *transport;
};
