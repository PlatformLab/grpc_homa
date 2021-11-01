#include <memory>

#include "homa.h"
#include "homa_stream.h"
#include "time_trace.h"
#include "util.h"


/**
 * This class is needed because grpc_core::SliceBufferByteStream::Orphan
 * doesn't actually delete the object; this class is identical to
 * grpc_core::SliceBufferByteStream except that its Orphan method
 * finishes the job.
 */
class HomaSliceBufferByteStream : public grpc_core::SliceBufferByteStream {
public:
    HomaSliceBufferByteStream(grpc_slice_buffer* slice_buffer, uint32_t flags)
            : grpc_core::SliceBufferByteStream(slice_buffer, flags)
    {}

    void Orphan() override
    {
        grpc_core::SliceBufferByteStream::Orphan();
        delete this;
    }
};

/**
 * Constructor for HomaStreams.
 * \param streamId
 *      Identifies a particular gRPC RPC.
 * \param fd
 *      Use this file descriptor to send and receive Homa messages.
 * \param refcount
 *      Used to determine when the stream can be destroyed.
 * \param arena
 *      Can be used for storage allocation by the stream; anything
 *      allocated here will persist for the life of the stream and be
 *      automatically garbage collected when the stream is destroyed.
 */
HomaStream::HomaStream(StreamId streamId, int fd,
        grpc_stream_refcount* refcount, grpc_core::Arena* arena)
    : mutex()
    , fd(fd)
    , streamId(streamId)
    , sentHomaId(0)
    , homaRequestId(0)
    , refs(refcount)
    , arena(arena)
    , xmitBuffer()
    , xmitOverflows()
    , overflowChunkSize(10000)
    , vecs()
    , lastVecAvail(0)
    , xmitSize(0)
    , nextXmitSequence(1)
    , incoming()
    , nextIncomingSequence(1)
    , messageData()
    , initMd(nullptr)
    , initMdClosure(nullptr)
    , messageStream(nullptr)
    , messageClosure(nullptr)
    , trailMd(nullptr)
    , trailMdClosure(nullptr)
    , eof(false)
    , cancelled(false)
    , error(GRPC_ERROR_NONE)
    , maxMessageLength(HOMA_MAX_MESSAGE_LENGTH)
{
    grpc_slice_buffer_init(&messageData);
    resetXmit();
}
    
HomaStream::~HomaStream()
{
    if (homaRequestId != 0) {
        sendDummyResponse();
    }
    for (grpc_slice &slice: slices) {
        grpc_slice_unref(slice);
    }
    grpc_slice_buffer_destroy(&messageData);
    GRPC_ERROR_UNREF(error);
}

/**
 * If any outgoing data has accumulated in this HomaStream, send it
 * as a Homa request or response (depending on the state of the stream).
 */
void HomaStream::flush()
{
    int status;
    if (((xmitSize <= sizeof(Wire::Header)) && (hdr()->flags == 0))
            || cancelled) {
        return;
    }
    bool isRequest = (homaRequestId == 0);
    
    // When transmitting data, send a response message if there is a
    // request we haven't yet responded to; otherwise start a fresh
    // request. This makes the most efficient use of Homa messages.
    if (isRequest) {
        hdr()->flags |= Wire::Header::request;
        status = homa_sendv(fd, vecs.data(), vecs.size(),
                reinterpret_cast<struct sockaddr *>(streamId.addr),
                streamId.addrSize, &sentHomaId);
        gpr_log(GPR_INFO, "Sent Homa request for stream id %d, "
                "sequence %d with Homa id %lu, %d initial metadata bytes, "
                "%d payload bytes, %d trailing metadata bytes",
                streamId.id, ntohl(hdr()->sequenceNum), sentHomaId,
                ntohl(hdr()->initMdBytes), ntohl(hdr()->messageBytes),
                ntohl(hdr()->trailMdBytes));
    } else {
        gpr_log(GPR_INFO, "Sending Homa response for stream id %d, "
                "sequence %d with Homa id %lu, %d initial metadata bytes, "
                "%d payload bytes, %d trailing metadata bytes",
                streamId.id, ntohl(hdr()->sequenceNum), homaRequestId,
                ntohl(hdr()->initMdBytes), ntohl(hdr()->messageBytes),
                ntohl(hdr()->trailMdBytes));
        status = homa_replyv(fd, vecs.data(), vecs.size(),
                reinterpret_cast<struct sockaddr *>(streamId.addr),
                streamId.addrSize, homaRequestId);
        homaRequestId = 0;
    }
    if (status < 0) {
        gpr_log(GPR_ERROR, "Couldn't send Homa %s: %s",
                (isRequest) ? "request" : "response",
                strerror(errno));
        error = GRPC_OS_ERROR(errno, "Couldn't send Homa request/response");
    }
    tt("Homa message sent");
    
    // It isn't safe to free the slices for message data until all
    // message data has been transmitted: otherwise, the last slice
    // may be needed for the next Homa message.
    if (hdr()->flags & Wire::Header::messageComplete) {
        for (grpc_slice &slice: slices) {
            grpc_slice_unref(slice);
        }
        slices.clear();
    }
    resetXmit();
}

/**
 * Reset all of the state related to an outgoing Homa message to start a
 * new message; any existing state is discarded.
 */
void HomaStream::resetXmit()
{
    new(hdr()) Wire::Header(streamId.id, nextXmitSequence);
    nextXmitSequence++;
    vecs.clear();
    vecs.push_back({xmitBuffer, sizeof(Wire::Header)});
    xmitSize = sizeof(Wire::Header);
    lastVecAvail = sizeof(xmitBuffer) - xmitSize;
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
        initMd = op->payload->recv_initial_metadata.recv_initial_metadata;
        initMdClosure =
                op->payload->recv_initial_metadata.recv_initial_metadata_ready;
        initMdTrailMdAvail =
                op->payload->recv_initial_metadata.trailing_metadata_available;
    }
    if (op->recv_message) {
        messageStream = op->payload->recv_message.recv_message;
        messageClosure = op->payload->recv_message.recv_message_ready;
    }
    if (op->recv_trailing_metadata) {
        trailMd = op->payload->recv_trailing_metadata.recv_trailing_metadata;
        trailMdClosure =
                op->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
    }
    transferData();
    
    if (error != GRPC_ERROR_NONE) {
        notifyError(GRPC_ERROR_REF(error));
    }
}

/**
 * Return the number of bytes required to serialize a batch of metadata
 * into a Homa message.
 * \param batch
 *      Metadata of interest.
 */
size_t HomaStream::metadataLength(grpc_metadata_batch* batch)
{
    size_t length = 0;
    for (grpc_linked_mdelem* md = batch->list.head; md != nullptr;
            md = md->next) {
        uint32_t keyLength = GRPC_SLICE_LENGTH(GRPC_MDKEY(md->md));
        uint32_t valueLength = GRPC_SLICE_LENGTH(GRPC_MDVALUE(md->md));
        length += keyLength + valueLength + sizeof(Wire::Mdata);
    }
    return length;
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
            size_t newSize = overflowChunkSize;
            if (elemLength > newSize) {
                newSize = elemLength;
            }
            cur = new uint8_t[newSize];
            xmitOverflows.push_back(cur);
            vecs.push_back({cur, 0});
            vec = &vecs.back();
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
 * Transmit the initial metadata, message, and trailing metadata that is
 * present in a batch of stream ops. May leave error information in
 * the stream's @error variable.
 * \param op
 *      Describes operations to perform on the stream.
 */
void HomaStream::xmit(grpc_transport_stream_op_batch* op)
{
    if (op->send_initial_metadata) {
        if (hdr()->initMdBytes) {
            flush();
        }
        size_t oldLength = xmitSize;
        serializeMetadata(
                op->payload->send_initial_metadata.send_initial_metadata);
        hdr()->initMdBytes = htonl(xmitSize - oldLength);
        hdr()->flags |= Wire::Header::initMdPresent;
        if (xmitSize > maxMessageLength) {
            gpr_log(GPR_ERROR, "Too much initial metadata (%lu bytes): "
                    "limit is %lu bytes", xmitSize - sizeof(Wire::Header),
                    maxMessageLength - sizeof(Wire::Header));
            error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                    "Too much initial metadata");
            return;
        }
    }
    
    size_t trailMdLength = 0;
    if (op->send_trailing_metadata) {
        trailMdLength = metadataLength(
                op->payload->send_trailing_metadata.send_trailing_metadata);
    }
    if (trailMdLength > (maxMessageLength - sizeof(Wire::Header))) {
        gpr_log(GPR_ERROR, "Too much trailing metadata (%lu bytes): "
                "limit is %lu bytes", trailMdLength,
                maxMessageLength - sizeof(Wire::Header));
        error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                "Too much trailing metadata");
        return;
    }
    
    // The trailing metadata and message data have to be handled together: if
    // they don't all fit in the initial Homa message, we first fill one or more
    // Homa messages with just message data. Then, in the final message, we
    // append the metadata *before* any final chunk of data (so it can use
    // the same iovec as the header.
    grpc_core::ByteStream *data = op->payload->send_message.send_message.get();
    size_t msgDataLeft = 0;
    size_t bytesInSlice = 0;
    size_t sliceOffset = 0;
    if (op->send_message) {
        tt("send_message stream op invoked");
        msgDataLeft = data->length();
    }
    while ((trailMdLength + msgDataLeft) > 0) {
        if ((xmitSize == maxMessageLength)
                || ((msgDataLeft == 0)
                && ((xmitSize + trailMdLength) > maxMessageLength))) {
            flush();
        }
        if (((xmitSize + trailMdLength + msgDataLeft) < maxMessageLength)
                && (trailMdLength > 0)) {
            // This will be the last Homa message, so output trailing metadata.
            serializeMetadata(
                    op->payload->send_trailing_metadata.send_trailing_metadata);
            hdr()->trailMdBytes = htonl(trailMdLength);
            trailMdLength = 0;
        }
        if (msgDataLeft > 0) {
            if (bytesInSlice == 0) {
                if (!data->Next(msgDataLeft, nullptr)) {
                    /* Should never reach here */
                    GPR_ASSERT(false);
                }
                slices.resize(slices.size()+1);
                if (data->Pull(&slices.back()) != GRPC_ERROR_NONE) {
                    /* Should never reach here */
                    GPR_ASSERT(false);
                }
                bytesInSlice = GRPC_SLICE_LENGTH(slices.back());
                sliceOffset = 0;
            }
            size_t chunkSize = maxMessageLength - xmitSize;
            if (chunkSize >= bytesInSlice) {
                chunkSize = bytesInSlice;
            }
            vecs.push_back({GRPC_SLICE_START_PTR(slices.back()) + sliceOffset,
                    chunkSize});
            lastVecAvail = 0;
            bytesInSlice -= chunkSize;
            sliceOffset += chunkSize;
            msgDataLeft -= chunkSize;
            hdr()->messageBytes = htonl(ntohl(hdr()->messageBytes)
                    + chunkSize);
            xmitSize += chunkSize;
        }
    }
    
    if (op->send_message) {
        hdr()->flags |= Wire::Header::messageComplete;
        op->payload->send_message.send_message.reset();
    }
    
    if (op->send_trailing_metadata) {
        hdr()->flags |= Wire::Header::trailMdPresent;
    }
    
    // If there's nothing besides initial metadata, don't flush now; wait
    // until it can be combined with something else.
    if ((hdr()->flags & Wire::Header::trailMdPresent)
            || (hdr()->messageBytes != 0)) {
        flush();
    }
}

/**
 * This method is called to send a response to homaRequestId, which
 * allows the kernel to clean up RPC state associated with it. The
 * caller must ensure that homaRequestId is valid.
 */
void HomaStream::sendDummyResponse()
{
    Wire::Header response(streamId.id, 0);
    response.flags |= Wire::Header::emptyResponse;
    gpr_log(GPR_INFO, "Sending dummy response for homaId %lu, stream id %d",
            homaRequestId, streamId.id);
    if (homa_reply(fd, &response, sizeof(response), streamId.sockaddr(),
            streamId.addrSize, homaRequestId) < 0) {
        gpr_log(GPR_ERROR, "Couldn't send dummy Homa response: %s",
                strerror(errno));
    }
    homaRequestId = 0;
}

/**
 * See if there is any information in accumulated incoming messages that
 * can now be passed off to gRPC. If so, pass it off.
 */
void HomaStream::transferData()
{
    /* A gRPC message consists of one or more Homa messages. The first
     * Homa message will contain all of the initial metadata,if
     * any, and as much message data as will fit. If the gRPC message
     * data is large, then there may be additional Homa messages with
     * more data. The last Homa message will contain the trailing
     * metadata.
     */
    while (incoming.size() > 0) {
        HomaIncoming *msg = incoming[0].get();
        if (msg->sequence > nextIncomingSequence) {
            break;
        }
        
        // Transfer initial metadata, if possible.
        if (initMdClosure) {
            if (msg->hdr()->flags & Wire::Header::initMdPresent) {
                tt("calling deserializeMetadata");
                msg->deserializeMetadata(sizeof(Wire::Header),
                        msg->initMdLength, initMd, arena);
                logMetadata(initMd, "incoming initial metadata");
                if (initMdTrailMdAvail && (msg->hdr()->flags
                        & msg->hdr()->trailMdPresent)) {
                    *initMdTrailMdAvail = true;
                }
                grpc_closure *c = initMdClosure;
                initMdClosure = nullptr;
                msg->hdr()->flags &= ~Wire::Header::initMdPresent;
                tt("Invoking initial metadata closure");
                grpc_core::ExecCtx::Run(DEBUG_LOCATION, c, GRPC_ERROR_NONE);
            }
        }

        // Transfer data, if possible.
        if (messageClosure) {
            if (msg->messageLength > 0) {
                // Add a slice for any message data in the initial payload.
                size_t msgOffset = sizeof(Wire::Header) + msg->initMdLength
                        + msg->trailMdLength;
                ssize_t initialLength = msg->baseLength - msgOffset;
                if (initialLength > 0) {
                    if (initialLength > msg->messageLength) {
                        initialLength = msg->messageLength;
                    }
                    grpc_slice_buffer_add(&messageData,
                            msg->getSlice(msgOffset, initialLength));
                    msgOffset += initialLength;
                } else {
                    initialLength = 0;
                }

                // Add a slice for any message data in the tail.
                if (initialLength < msg->messageLength) {
                    grpc_slice_buffer_add(&messageData,
                        msg->getSlice(msgOffset,
                        msg->messageLength - initialLength));
                }
                msg->messageLength = 0;
            }
            
            if (msg->hdr()->flags & Wire::Header::messageComplete) {
                messageStream->reset(new HomaSliceBufferByteStream(
                        &messageData, 0));
                grpc_slice_buffer_destroy(&messageData);
                grpc_slice_buffer_init(&messageData);

                grpc_closure *c = messageClosure;
                messageClosure = nullptr;
                msg->hdr()->flags &= ~Wire::Header::messageComplete;
                tt("Invoking message closure");
                grpc_core::ExecCtx::Run(DEBUG_LOCATION, c, GRPC_ERROR_NONE);
            }
        }

        // Transfer trailing metadata, if possible.
        if (msg->hdr()->flags & Wire::Header::trailMdPresent) {
            eof = true;
            if (trailMdClosure) {
                msg->deserializeMetadata(
                        sizeof(Wire::Header) + msg->initMdLength,
                        msg->trailMdLength, trailMd, arena);
                logMetadata(trailMd, "incoming trailing metadata");
                grpc_closure *c = trailMdClosure;
                trailMdClosure = nullptr;
                msg->hdr()->flags &= ~Wire::Header::trailMdPresent;
                tt("Invoking trailing metadata closure");
                gpr_log(GPR_INFO,"Invoked trailing metadata closure for "
                        "stream id %d", streamId.id);
                grpc_core::ExecCtx::Run(DEBUG_LOCATION, c, GRPC_ERROR_NONE);
            }
        }
        
        if ((msg->hdr()->flags & (Wire::Header::initMdPresent
                | Wire::Header::trailMdPresent | Wire::Header::messageComplete))
                || (msg->messageLength != 0)) {
            break;
        }
        
        // We've extracted everything of value from this message.
        if (msg->sequence == nextIncomingSequence) {
            nextIncomingSequence++;
        }
        incoming.erase(incoming.begin());
    }
    
    if (eof && messageClosure) {
        // gRPC has asked for another message but there aren't going to
        // be any; signal that.
        *messageStream = nullptr;
        grpc_closure *c = messageClosure;
        messageClosure = nullptr;
        grpc_core::ExecCtx::Run(DEBUG_LOCATION, c, GRPC_ERROR_NONE);
        gpr_log(GPR_INFO, "Invoked message closure (eof)");
    }
}

/**
 * Take ownership of an incoming message and ensure that (eventually)
 * all of the information in it is passed to gRPC.
 * \param msg
 *      An incoming Homa message previously returned by readIncoming. This
 *      method takes ownership of the message (we may need to save part or
 *      all of it for a while before the info can be passed to gRPC).
 * \param homaId
 *      Homa identifier for the RPC used to transmit this message.
 */
void HomaStream::handleIncoming(HomaIncoming::UniquePtr msg, uint64_t homaId)
{
    if (msg->hdr()->flags & Wire::Header::request) {
        if (homaRequestId != 0){
            // We only have room to save one request id, so clean up the
            // older request.
            sendDummyResponse();
        }
        homaRequestId = homaId;
    }
    
    if (msg->hdr()->flags & Wire::Header::cancelled) {
        gpr_log(GPR_INFO, "RPC id %d cancelled by peer", streamId.id);
        cancelled = true;
        notifyError(GRPC_ERROR_CANCELLED);
        return;
    }
    
    if (incoming.empty()) {
        incoming.push_back(std::move(msg));
    } else {
        for (size_t i = incoming.size(); i > 0; i--) {
            if (incoming[i-1]->sequence < msg->sequence) {
                incoming.emplace(incoming.begin()+i, std::move(msg));
                goto messageInserted;
            }
        }
        incoming.emplace(incoming.begin(), std::move(msg));
    }
messageInserted:
    transferData();
}

/**
 * This method is invoked when a fatal error occurs on a stream. It invokes
 * any callbacks present for the stream.
 * \param error
 *      Information about what went wrong. This method takes ownership
 *      of the error.
 */
void HomaStream::notifyError(grpc_error_handle error)
{
    GRPC_ERROR_UNREF(this->error);
    this->error = error;
    gpr_log(GPR_INFO, "Recording error for stream id %u: %s",
        streamId.id, grpc_error_string(error));
    
    if (initMdClosure) {
        grpc_closure *c = initMdClosure;
        initMdClosure = nullptr;
        grpc_core::ExecCtx::Run(DEBUG_LOCATION, c, GRPC_ERROR_REF(this->error));
    }
    if (messageClosure) {
        grpc_closure *c = messageClosure;
        messageClosure = nullptr;
        grpc_core::ExecCtx::Run(DEBUG_LOCATION, c, GRPC_ERROR_REF(this->error));
    }
    if (trailMdClosure) {
        grpc_closure *c = trailMdClosure;
        trailMdClosure = nullptr;
        grpc_core::ExecCtx::Run(DEBUG_LOCATION, c, GRPC_ERROR_REF(this->error));
    }
}

/**
 * Notify the other side that this RPC is being cancelled.
 */
void HomaStream::cancelPeer(void)
{
    if (cancelled) {
        return;
    }
    gpr_log(GPR_INFO, "Sending peer cancellation for RPC id %d", streamId.id);
    hdr()->flags |= Wire::Header::cancelled;
    flush();
    cancelled = true;
}