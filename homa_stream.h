#ifndef HOMA_STREAM_H
#define HOMA_STREAM_H

#include "src/core/lib/transport/transport_impl.h"

#include "rpc_id.h"
#include "wire.h"

/**
 * This is a base class that provides common facilities used for Homa
 * streams on both the client and server side. Each stream object
 * corresponds to a single RPC, and it exists for the life of that
 * RPC.
 */
class HomaStream {
public:
    // Uniquely identifies this RPC, and also provides info about
    // the peer (e.g. for sending responses).
    RpcId rpcId;

    // The Homa RPC used for the initial request (and also for the
    // final response). Other Homa RPCs may be used for intermediate
    // data. 0 means id not known yet.
    uint64_t homaId;

    // Reference count (owned externally).
    grpc_stream_refcount* refs;

    // For fast memory allocation.
    grpc_core::Arena* arena;

    // Information saved from "receive" stream ops, so that we can
    // fill in message data/metadata and invoke callbacks.
    grpc_metadata_batch* initMd;
    grpc_closure* initMdClosure;
    grpc_core::OrphanablePtr<grpc_core::ByteStream>* messageStream;
    grpc_closure* messageClosure;
    grpc_metadata_batch* trailMd;
    grpc_closure* trailMdClosure;

    HomaStream(RpcId rpcId, uint64_t homaId, grpc_stream_refcount* refcount,
            grpc_core::Arena* arena)
        : rpcId(rpcId)
        , homaId(homaId)
        , refs(refcount)
        , arena(arena)
        , initMd(nullptr)
        , initMdClosure(nullptr)
        , messageStream(nullptr)
        , messageClosure(nullptr)
        , trailMd(nullptr)
        , trailMdClosure(nullptr)
    {}

    virtual ~HomaStream() {}
    void saveCallbacks(grpc_transport_stream_op_batch* op);
    void transferDataIn(Wire::Message* msg);
};

#endif // HOMA_STREAM_H
