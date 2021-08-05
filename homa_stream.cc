#include "homa_stream.h"
#include "util.h"

/**
 * This method records callback information from a stream op, so that
 * it can be used to transfer data and invoke callbacks later (via
 * transferDataIn).
 * \param op
 *      A gRPC stream op; may contain callback info.
 */
void HomaStream::saveCallbacks(grpc_transport_stream_op_batch* op)
{
    if (op->recv_initial_metadata) {
        gpr_log(GPR_INFO, "HomaListener::perform_stream_op: "
                "receive initial metadata");
        initMd = op->payload->recv_initial_metadata.recv_initial_metadata;
        initMdClosure =
                op->payload->recv_initial_metadata.recv_initial_metadata_ready;
    }
    if (op->recv_message) {
        gpr_log(GPR_INFO, "HomaListener::perform_stream_op: receive message");
        messageStream = op->payload->recv_message.recv_message;
        messageClosure = op->payload->recv_message.recv_message_ready;
    }
    if (op->recv_trailing_metadata) {
        gpr_log(GPR_INFO, "HomaListener::perform_stream_op: "
                "receive trailing metadata");
        trailMd = op->payload->recv_trailing_metadata.recv_trailing_metadata;
        trailMdClosure =
                op->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
    }
}

/**
 * Transfer data and/or metadata from an incoming message into gRPC, if the
 * callbacks are present.
 * \param msg
 *      Incoming message.
 */
void HomaStream::transferDataIn(Wire::Message* msg)
{
    uint32_t initMdLength = htonl(msg->hdr.initMdBytes);
    uint32_t messageLength = htonl(msg->hdr.messageBytes);
    uint32_t trailMdLength = htonl(msg->hdr.trailMdBytes);
    uint8_t* src = msg->payload;
    if (initMdClosure) {
        Wire::deserializeMetadata(src, initMdLength, initMd, arena);
        logMetadata(initMd, "server initial metadata");
        grpc_closure *c = initMdClosure;
        initMdClosure = nullptr;
        grpc_core::ExecCtx::Run(DEBUG_LOCATION, c, GRPC_ERROR_NONE);
    }
    src += initMdLength;
    if (messageClosure) {
        grpc_slice_buffer sb;
        grpc_slice_buffer_init(&sb);
        grpc_slice_buffer_add(&sb, grpc_slice_from_static_buffer(
                src, messageLength));
        messageStream->reset(new grpc_core::SliceBufferByteStream(&sb, 0));
        grpc_slice_buffer_destroy(&sb);
        
        grpc_closure *c = messageClosure;
        messageClosure = nullptr;
        grpc_core::ExecCtx::Run(DEBUG_LOCATION, c, GRPC_ERROR_NONE);
    }
    src += messageLength;
    if (trailMdClosure) {
        Wire::deserializeMetadata(src, trailMdLength, trailMd, arena);
        grpc_closure *c = trailMdClosure;
        trailMdClosure = nullptr;
        grpc_core::ExecCtx::Run(DEBUG_LOCATION, c, GRPC_ERROR_NONE);
    }
}
