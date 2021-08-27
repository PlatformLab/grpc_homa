#include "homa.h"
#include "homa_stream.h"
#include "util.h"

/**
 * Constructor for HomaStreams.
 * \param streamId
 *      Identifies a particular gRPC RPC.
 * \param homaId
 *      On servers, the Homa identifier for the first message; on clients,
 *      zero.
 * \param fd
 *      Use this file descriptor to send and receive Homa messages.
 * \param refcount
 *      Used to determine when the stream can be destroyed.
 * \param arena
 *      Can be used for storage allocation by the stream; anything
 *      allocated here will persist for the life of the stream and be
 *      automatically garbage collected when the stream is destroyed.
 */
HomaStream::HomaStream(StreamId streamId, uint64_t homaId, int fd,
        grpc_stream_refcount* refcount, grpc_core::Arena* arena)
    : mutex()
    , fd(fd)
    , streamId(streamId)
    , homaId(homaId)
    , refs(refcount)
    , arena(arena)
    , xmitMsg()
    , xmitOverflows()
    , vecs()
    , lastVecAvail(0)
    , xmitSize(0)
    , nextSequence(0)
    , initMd(nullptr)
    , initMdClosure(nullptr)
    , messageStream(nullptr)
    , messageClosure(nullptr)
    , trailMd(nullptr)
    , trailMdClosure(nullptr)
    , error(GRPC_ERROR_NONE)
{}
    
HomaStream::~HomaStream()
{
    GRPC_ERROR_UNREF(error);
}

/**
 * If any outgoing data has accumulated in this HomaStream, send it
 * as a Home request or response (depending on the state of the stream).
 */
void HomaStream::flush()
{
    int status;
    if ((xmitSize <= sizeof(xmitMsg.hdr)) && (xmitMsg.hdr.flags == 0)) {
        return;
    }
    if (homaId == 0) {
        status = homa_sendv(fd, vecs.data(), vecs.size(),
                reinterpret_cast<struct sockaddr *>(streamId.addr),
                streamId.addrSize, nullptr);
    } else {
        status = homa_replyv(fd, vecs.data(), vecs.size(),
                reinterpret_cast<struct sockaddr *>(streamId.addr),
                streamId.addrSize, homaId);
    }
    if (status < 0) {
        gpr_log(GPR_ERROR, "Couldn't send Homa %s: %s",
                (homaId == 0) ? "request" : "response",
                strerror(errno));
        error = GRPC_OS_ERROR(errno, "Couldn't send Homa request/response");
    } else {
        gpr_log(GPR_INFO, "Sent Homa %s with %d initial metadata bytes, "
                "%d payload bytes, %d trailing metadata bytes",
                (homaId == 0) ? "request" : "response",
                ntohl(xmitMsg.hdr.initMdBytes),
                ntohl(xmitMsg.hdr.messageBytes),
                ntohl(xmitMsg.hdr.trailMdBytes));
    }
    for (grpc_slice &slice: slices) {
        grpc_slice_unref(slice);
    }
    slices.clear();
}

/**
 * Reset all of the state related to an outgoing Homa message to start a
 * new message; any existing state is discarded.
 */
void HomaStream::newXmit()
{
    new(&xmitMsg.hdr) Wire::Header(streamId.id, nextSequence);
    nextSequence++;
    vecs.clear();
    vecs.push_back({&xmitMsg, sizeof(xmitMsg.hdr)});
    xmitSize = sizeof(xmitMsg.hdr);
    lastVecAvail = sizeof(xmitMsg) - xmitSize;
}

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
 * Serialize a batch of metadata and append it to the output message
 * currently being formed.
 * \param batch
 *      All of the key-value pairs here will be appended.
 */
void HomaStream::serializeMetadata(grpc_metadata_batch* batch)
{
    struct iovec *vec = &vecs.back();
    uint8_t *cur = static_cast<uint8_t *>(vec->iov_base) + vec->iov_len;
    for (grpc_linked_mdelem* md = batch->list.head; md != nullptr;
            md = md->next) {
        const grpc_slice& key = GRPC_MDKEY(md->md);
        const grpc_slice& value = GRPC_MDVALUE(md->md);
        uint32_t keyLength = GRPC_SLICE_LENGTH(key);
        uint32_t valueLength = GRPC_SLICE_LENGTH(value);
        uint32_t elemLength = keyLength + valueLength + sizeof(Wire::Mdata);
        if (elemLength > lastVecAvail) {
            // Add an overflow chunk to the outgoing Homa message.
            size_t newSize = 10000;
            if (elemLength > newSize) {
                newSize = elemLength;
            }
            cur = new uint8_t[newSize];
            xmitOverflows.push_back(cur);
            vecs.push_back({cur, 0});
            lastVecAvail = newSize;
        }
        
        Wire::Mdata* msgMd = reinterpret_cast<Wire::Mdata*>(cur);
        msgMd->index = GRPC_BATCH_INDEX_OF(key);
        gpr_log(GPR_INFO, "Outgoing metadata: index %d, key %.*s, value %.*s",
                msgMd->index, keyLength, GRPC_SLICE_START_PTR(key),
                valueLength, GRPC_SLICE_START_PTR(value));
        msgMd->keyLength = htonl(keyLength);
        msgMd->valueLength = htonl(valueLength);
        cur += sizeof(*msgMd);
        memcpy(cur, GRPC_SLICE_START_PTR(key), keyLength);
        cur += keyLength;
        memcpy(cur, GRPC_SLICE_START_PTR(value), valueLength);
        cur += valueLength;
        vec->iov_len += elemLength;
        lastVecAvail -= elemLength;
        xmitSize += elemLength;
    }
}

/**
 * Append gRPC message data to the current Homa message. If the maximum Homa
 * message length is exceeded, transmit full message(s) and start new one(s)
 * \param op
 *      Contains information about the message data to append.
 *      
 */
void HomaStream::appendMessage(grpc_transport_stream_op_batch* op)
{
    uint32_t bytesLeft = op->payload->send_message.send_message->length();
    size_t prevSize = xmitSize;
    while (bytesLeft != 0) {
        if (!op->payload->send_message.send_message->Next(bytesLeft,
                nullptr)) {
            /* Should never reach here */
            GPR_ASSERT(false);
        }
        slices.resize(slices.size()+1);
        grpc_slice &slice = slices.back();
        if (op->payload->send_message.send_message->Pull(&slice)
                != GRPC_ERROR_NONE) {
            /* Should never reach here */
            GPR_ASSERT(false);
        }
        uint8_t *cur = GRPC_SLICE_START_PTR(slice);
        size_t sliceLeft = GRPC_SLICE_LENGTH(slice);
        
        // A given slice might have to be split across multiple
        // Homa messages. Each iteration of this loop corresponds
        // to one Homa message (and one iovec).
        while (sliceLeft > 0) {
            size_t chunkSize;
            while (true) {
                chunkSize = HOMA_MAX_MESSAGE_LENGTH - xmitSize;
                if (chunkSize >= sliceLeft) {
                    chunkSize = sliceLeft;
                    break;
                }
                if (chunkSize > 0) {
                    break;
                }

                // Current message has reached Homa's limit; transmit it
                // and start a new one.
                xmitMsg.hdr.messageBytes = ntohl(xmitSize - prevSize);
                flush();
                newXmit();
                prevSize = xmitSize;
            }
            vecs.push_back({cur, chunkSize});
            lastVecAvail = 0;
            cur += chunkSize;
            sliceLeft -= chunkSize;
            xmitSize += chunkSize;
            xmitMsg.hdr.messageBytes += chunkSize;
        }
        
        bytesLeft -= GRPC_SLICE_LENGTH(slice);
    }
    xmitMsg.hdr.messageBytes = ntohl(xmitSize - prevSize);
}

/**
 * Transmit the initial metadata, message, and trailing metadata that is
 * present in a batch of stream ops.
 * \param op
 *      Describes operations to perform on the stream.
 */
void HomaStream::xmit(grpc_transport_stream_op_batch* op)
{
    newXmit();
    
    if (op->send_initial_metadata) {
        size_t oldLength = xmitSize;
        serializeMetadata(
                op->payload->send_initial_metadata.send_initial_metadata);
        xmitMsg.hdr.initMdBytes = htonl(xmitSize - oldLength);
        xmitMsg.hdr.flags |= WIRE_INIT_MD_COMPLETE;
    }
    
    if (op->send_trailing_metadata) {
        size_t oldLength = xmitSize;
        serializeMetadata(
                op->payload->send_trailing_metadata.send_trailing_metadata);
        xmitMsg.hdr.trailMdBytes = htonl(xmitSize - oldLength);
        xmitMsg.hdr.flags |= WIRE_TRAIL_MD_COMPLETE;
    }
    
    if (xmitSize > HOMA_MAX_MESSAGE_LENGTH) {
        error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                "Metadata exceeded maximum Homa message size");
        return;
    }

    if (op->send_message) {
        appendMessage(op);
        if (error != GRPC_ERROR_NONE) {
            return;
        }
    }
    flush();
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
    gpr_log(GPR_INFO, "Incoming message: initMdLength %d msgLength %d "
            "trailingMdLength %d", initMdLength, messageLength, trailMdLength);
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
                src+trailMdLength, messageLength));
        messageStream->reset(new grpc_core::SliceBufferByteStream(&sb, 0));
        grpc_slice_buffer_destroy(&sb);
        
        grpc_closure *c = messageClosure;
        messageClosure = nullptr;
        grpc_core::ExecCtx::Run(DEBUG_LOCATION, c, GRPC_ERROR_NONE);
    }
    if (trailMdClosure) {
        Wire::deserializeMetadata(src, trailMdLength, trailMd, arena);
        grpc_closure *c = trailMdClosure;
        trailMdClosure = nullptr;
        grpc_core::ExecCtx::Run(DEBUG_LOCATION, c, GRPC_ERROR_NONE);
    }
    src += trailMdLength;
}
