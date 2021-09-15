#ifndef HOMA_STREAM_H
#define HOMA_STREAM_H

#include <sys/uio.h>

#include <vector>

#include "src/core/lib/transport/transport_impl.h"

#include "homa_incoming.h"
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

    // The identifier for the first RPC on this stream (which is also used
    // for the final response). On clients, this is 0 until the first RPC
    // is sent.
    uint64_t homaId;
    
    // True means that this is the client side of the RPC; false means
    // server side.
    bool isClient;

    // Reference count (owned externally).
    grpc_stream_refcount* refs;

    // For fast memory allocation.
    grpc_core::Arena* arena;
    
    // Small statically allocated buffer for outgoing messages; holds
    // header plus initial and trailing metadata, if they fit.
    uint8_t xmitBuffer[10000];
    
    // If the metadata didn't completely fit in xmit_msg, extra chunks
    // are allocated dynamically; this vector keeps track of them all
    // so they can be freed.
    std::vector<uint8_t *> xmitOverflows;
    
    // How many bytes to allocate for each element of @xmitOverflows.
    // This is a variable so it can be changed for unit testing.
    size_t overflowChunkSize;
    
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
    int nextXmitSequence;
    
    // Incoming Homa messages that have not been fully processed.
    // Entries are sorted in increasing order of sequence number.
    std::vector<HomaIncoming::UniquePtr> incoming;
    
    // All incoming Homa messages with sequence numbers less than this one
    // have already been processed.
    int nextIncomingSequence;
    
    // Accumulates slices of incoming message data (potentially from
    // multiple Homa messages) until they can be passed to gRPC.
    grpc_slice_buffer messageData;

    // Information saved from "receive" stream ops, so that we can
    // fill in message data/metadata and invoke callbacks.
    grpc_metadata_batch* initMd;
    grpc_closure* initMdClosure;
    grpc_core::OrphanablePtr<grpc_core::ByteStream>* messageStream;
    grpc_closure* messageClosure;
    grpc_metadata_batch* trailMd;
    grpc_closure* trailMdClosure;
    
    // True means we have passed trailing metadata to gRPC, so there is
    // no more message data coming for this stream.
    bool eof;
    
    // Error that has occurred on this stream, if any.
    grpc_error_handle error;
    
    // Maximum number of bytes to allow in a single Homa message (this
    // is a variable so it can be modified for unit testing).
    size_t maxMessageLength;

    HomaStream(StreamId streamId, uint64_t homaId, int fd,
            grpc_stream_refcount* refcount, grpc_core::Arena* arena);
    
    Wire::Header *hdr()
    {
        return reinterpret_cast<Wire::Header*>(xmitBuffer);
    }

    virtual ~HomaStream();
    void    flush();
    void    handleIncoming(HomaIncoming::UniquePtr msg);
    void    newXmit();
    void    notifyError(grpc_error_handle error);
    void    saveCallbacks(grpc_transport_stream_op_batch* op);
    void    serializeMetadata(grpc_metadata_batch* batch);
    void    transferData();
    void    xmit(grpc_transport_stream_op_batch* op);
    
    static size_t metadataLength(grpc_metadata_batch* batch);
};

#endif // HOMA_STREAM_H
