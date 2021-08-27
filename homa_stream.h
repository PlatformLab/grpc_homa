#ifndef HOMA_STREAM_H
#define HOMA_STREAM_H

#include <sys/uio.h>

#include <vector>

#include "src/core/lib/transport/transport_impl.h"

#include "stream_id.h"
#include "wire.h"

/**
 * This is a base class that provides common facilities used for Homa
 * streams on both the client and server side. Each stream object
 * corresponds to a single RPC, and it exists for the life of that
 * RPC. It is used to manage the movement of data between gRPC and
 * Homa messages, as well as gRPC callbacks.
 */
class HomaStream {
public:
    // Must be held whenever accessing info in this structure.
    grpc_core::Mutex mutex;
    
    // File descriptor for the Homa socket to use for I/O.
    int fd;
    
    // Uniquely identifies this gRPC RPC, and also provides info about
    // the peer (e.g. for sending responses).
    StreamId streamId;

    // On servers, this contains the identifier for the original Homa
    // request (which is also used for the final response). On clients,
    // this is zero.
    uint64_t homaId;

    // Reference count (owned externally).
    grpc_stream_refcount* refs;

    // For fast memory allocation.
    grpc_core::Arena* arena;
    
    // Small statically allocated buffer for outgoing messages; holds
    // header plus initial and trailing metadata, if they fit.
    Wire::Message xmitMsg;
    
    // If the metadata didn't completely fit in xmit_msg, extra chunks
    // are allocated dynamically; this vector keeps track of them all
    // so they can be freed.
    std::vector<uint8_t *> xmitOverflows;
    
    // Contains all of the slices (of message data) referred to by vecs;
    // keeps them alive and stable until the Homa request is sent.
    std::vector<grpc_slice> slices;
    
    // Describes all of the pieces of the current outgoing message.
    std::vector<struct iovec> vecs;
    
    // Additional bytes available immediately following the last element
    // of vecs.
    size_t lastVecAvail;
    
    // Current length of output message, in bytes.
    size_t xmitSize;
    
    // Sequence number to use for the next outgoing message.
    int nextSequence;
    
    // Holds an incoming request or response for this RPC, if any.
    std::unique_ptr<Wire::Message> incoming;

    // Information saved from "receive" stream ops, so that we can
    // fill in message data/metadata and invoke callbacks.
    grpc_metadata_batch* initMd;
    grpc_closure* initMdClosure;
    grpc_core::OrphanablePtr<grpc_core::ByteStream>* messageStream;
    grpc_closure* messageClosure;
    grpc_metadata_batch* trailMd;
    grpc_closure* trailMdClosure;
    
    // Error that has occurred on this stream, if any.
    grpc_error_handle error;

    HomaStream(StreamId streamId, uint64_t homaId, int fd,
            grpc_stream_refcount* refcount, grpc_core::Arena* arena);

    virtual ~HomaStream();
    void appendMessage(grpc_transport_stream_op_batch* op);
    void flush();
    void newXmit();
    void saveCallbacks(grpc_transport_stream_op_batch* op);
    void serializeMetadata(grpc_metadata_batch* batch);
    void transferDataIn(Wire::Message* msg);
    void xmit(grpc_transport_stream_op_batch* op);
};

#endif // HOMA_STREAM_H
