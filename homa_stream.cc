#include <memory>

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/slice/slice.h"

#include "homa.h"
#include "homa_stream.h"
#include "time_trace.h"
#include "util.h"

/**
 * Constructor for HomaStreams.
 * \param isServer
 *      True means this stream will be used for the server side of an RPC;
 *      false means client side.
 * \param streamId
 *      Identifies a particular gRPC RPC.
 * \param fd
 *      Use this file descriptor to send and receive Homa messages.
 * \param refcount
 *      Used to determine when the stream can be destroyed.
 */
HomaStream::HomaStream(bool isServer, StreamId streamId, int fd,
        grpc_stream_refcount* refcount)
    : mutex()
    , fd(fd)
    , streamId(streamId)
    , sentHomaId(0)
    , homaRequestId(0)
    , refs(refcount)
//    , xmitBuffer()            This isn't needed and seems to take a long time?
    , xmitOverflows()
    , overflowChunkSize(10000)
    , slices()
    , vecs()
    , lastVecAvail(0)
    , xmitSize(0)
    , nextXmitSequence(1)
    , incoming()
    , nextIncomingSequence(1)
    , initMd(nullptr)
    , initMdClosure(nullptr)
    , initMdTrailMdAvail(nullptr)
    , messageBody(nullptr)
    , messageClosure(nullptr)
    , trailMd(nullptr)
    , trailMdClosure(nullptr)
    , eof(false)
    , cancelled(false)
    , trailMdSent(false)
    , isServer(isServer)
    , error(absl::OkStatus())
    , maxMessageLength(HOMA_MAX_MESSAGE_LENGTH)
{
    resetXmit();
}

HomaStream::~HomaStream()
{
    if (homaRequestId != 0) {
        sendDummyResponse();
    }
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
        tt("Invoking homa_sendv");
        status = homa_sendv(fd, vecs.data(), vecs.size(),
                &streamId.addr, &sentHomaId, 0);
        if (gpr_should_log(GPR_LOG_SEVERITY_INFO)) {
            gpr_log(GPR_INFO, "Sent Homa request to %s, "
                    "sequence %d with homaId %lu, %d initial metadata bytes, "
                    "%d payload bytes, %d trailing metadata bytes",
                    streamId.toString().c_str(), ntohl(hdr()->sequenceNum),
                    sentHomaId, ntohl(hdr()->initMdBytes),
                    ntohl(hdr()->messageBytes), ntohl(hdr()->trailMdBytes));
        }
        tt("homa_sendv returned");
    } else {
        if (gpr_should_log(GPR_LOG_SEVERITY_INFO)) {
            gpr_log(GPR_INFO, "Sending Homa response to %s, "
                    "sequence %d with homaId %lu, %d initial metadata bytes, "
                    "%d payload bytes, %d trailing metadata bytes",
                    streamId.toString().c_str(), ntohl(hdr()->sequenceNum),
                    homaRequestId, ntohl(hdr()->initMdBytes),
                    ntohl(hdr()->messageBytes), ntohl(hdr()->trailMdBytes));
        }
        tt("Invoking homa_replyv");
        status = homa_replyv(fd, vecs.data(), vecs.size(), &streamId.addr,
                homaRequestId);
        tt("homa_replyv returned");
        homaRequestId = 0;
    }
    if (status < 0) {
        gpr_log(GPR_ERROR, "Couldn't send Homa %s: %s",
                (isRequest) ? "request" : "response",
                strerror(errno));
        error = GRPC_OS_ERROR(errno, "Couldn't send Homa request/response");
    }

    // It isn't safe to free the slices for message data until all
    // message data has been transmitted: otherwise, the last slice
    // may be needed for the next Homa message.
    if (hdr()->flags & Wire::Header::messageComplete) {
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
 * transferData).
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
        messageBody = op->payload->recv_message.recv_message;
        messageClosure = op->payload->recv_message.recv_message_ready;
    }
    if (op->recv_trailing_metadata) {
        trailMd = op->payload->recv_trailing_metadata.recv_trailing_metadata;
        trailMdClosure =
                op->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
    }

    if (!error.ok()) {
        notifyError(error);
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
    batch->Log([&length] (absl::string_view key, absl::string_view value) {
        length += key.length() + value.length() + sizeof(Wire::Mdata);
    });
    return length;
}

/**
 * Serialize a metadata value into the current output message for the stream.
 * \param key
 *      First byte of key (not necessarily null-terminated).
 * \param keyLength
 *      Number of bytes in key.
 * \param value
 *      First byte of value corresponding to @name (not necessarily
 *      null-terminated).
 * \param valueLength
 *      Number of bytes in value.
 */
void HomaStream::serializeMetadata(const void *key, uint32_t keyLength,
        const void *value, uint32_t valueLength)
{
    struct iovec *vec = &vecs.back();
    uint8_t *cur = static_cast<uint8_t *>(vec->iov_base) + vec->iov_len;
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
    gpr_log(GPR_INFO, "Outgoing metadata: key %.*s, value %.*s",
            keyLength, static_cast<const char *>(key), valueLength,
            static_cast<const char *>(value));
    msgMd->keyLength = htonl(keyLength);
    msgMd->valueLength = htonl(valueLength);
    cur += sizeof(*msgMd);
    memcpy(cur, key, keyLength);
    cur += keyLength;
    memcpy(cur, value, valueLength);
    cur += valueLength;
    vec->iov_len += elemLength;
    lastVecAvail -= elemLength;
    xmitSize += elemLength;
}

/**
 * Serialize a metadata batch into the current output message for the stream.
 * \param batch
 *      Collection of metadata entries to serialize.
 */
void HomaStream::serializeMetadataBatch(grpc_metadata_batch *batch)
{
    batch->Log([this] (absl::string_view key, absl::string_view value) {
        serializeMetadata(key.data(), key.length(), value.data(),
                value.length());
    });
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
        serializeMetadataBatch(op->payload->send_initial_metadata
                .send_initial_metadata);
        hdr()->initMdBytes = htonl(xmitSize - oldLength);
        hdr()->flags |= Wire::Header::initMdPresent;
        if (xmitSize > maxMessageLength) {
            gpr_log(GPR_ERROR, "Too much initial metadata (%lu bytes): "
                    "limit is %lu bytes", xmitSize - sizeof(Wire::Header),
                    maxMessageLength - sizeof(Wire::Header));
            error = GRPC_ERROR_CREATE("Too much initial metadata");
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
        error = GRPC_ERROR_CREATE("Too much trailing metadata");
        return;
    }

    // The trailing metadata and message data have to be handled together: if
    // they don't all fit in the initial Homa message, we first fill one or more
    // Homa messages with just message data. Then, in the final message, we
    // append the metadata *before* any final chunk of data (so it can use
    // the same iovec as the header.
    grpc_core::SliceBuffer *data = op->payload->send_message.send_message;
    size_t msgDataLeft = 0;
    size_t bytesInSlice = 0;
    size_t sliceOffset = 0;
    if (op->send_message) {
        msgDataLeft = data->Length();
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
            serializeMetadataBatch(op->payload->send_trailing_metadata
                    .send_trailing_metadata);
            hdr()->trailMdBytes = htonl(trailMdLength);
            trailMdLength = 0;
        }
        if (msgDataLeft > 0) {
            if (bytesInSlice == 0) {
                GPR_ASSERT(data->Count() > 0);
                slices.emplace_back(data->TakeFirst());
                bytesInSlice = slices.back().length();
                sliceOffset = 0;
            }
            size_t chunkSize = maxMessageLength - xmitSize;
            if (chunkSize >= bytesInSlice) {
                chunkSize = bytesInSlice;
            }
            vecs.push_back({const_cast<uint8_t *>(slices.back().data()
                    + sliceOffset), chunkSize});
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
    }

    if (op->send_trailing_metadata) {
        hdr()->flags |= Wire::Header::trailMdPresent;
    }

    // If there's nothing besides initial metadata, don't flush now; wait
    // until it can be combined with something else.
    if (op->send_message || op->send_trailing_metadata) {
        flush();
    }

    if (op->send_trailing_metadata) {
        trailMdSent = true;
        if (isServer) {
            transferData();
        }
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
    if (homa_reply(fd, &response, sizeof(response), &streamId.addr,
            homaRequestId) < 0) {
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
        Wire::Header headerStorage;
        Wire::Header *h = msg->get<Wire::Header>(0, &headerStorage);

        if (msg->sequence > nextIncomingSequence) {
            break;
        }

        // Transfer initial metadata, if possible.
        if (initMdClosure) {
            if (h->flags & Wire::Header::initMdPresent) {
                msg->deserializeMetadata(sizeof(Wire::Header),
                        msg->initMdLength, initMd);
                if (isServer) {
                    addPeerToMetadata(initMd);
                }
                logMetadata(initMd, "Incoming initial metadata");
                if (initMdTrailMdAvail && (h->flags
                        & h->trailMdPresent)) {
                    *initMdTrailMdAvail = true;
                }
                grpc_closure *c = initMdClosure;
                initMdClosure = nullptr;
                h->flags &= ~Wire::Header::initMdPresent;
                grpc_core::ExecCtx::Run(DEBUG_LOCATION, c, absl::OkStatus());
            }
        }

        // Transfer data, if possible.
        if (messageClosure) {
            uint32_t bytesLeft = msg->bodyLength;
            size_t msgOffset = sizeof(Wire::Header) + msg->initMdLength
                    + msg->trailMdLength;

            // Each iteration of this loop creates a slice for one
            // contiguous range of the message.
            if (!messageBody->has_value()) {
                messageBody->emplace();
            }
            while (bytesLeft > 0) {
                size_t sliceLength = msg->contiguous(msgOffset);
                if (sliceLength > bytesLeft) {
                    sliceLength = bytesLeft;
                }
                messageBody->value().Append(msg->getSlice(msgOffset, sliceLength));
                msgOffset += sliceLength;
                bytesLeft -= sliceLength;
            }
            msg->bodyLength = 0;

            if (h->flags & Wire::Header::messageComplete) {
                if (messageBody->value().Count() == 0) {
                    // This is a zero-length message; must create a
                    // zero-length slice.
                    grpc_slice slice;

                    slice.refcount = grpc_slice_refcount::NoopRefcount();
                    slice.data.refcounted.bytes =  NULL;
                    slice.data.refcounted.length = 0;
                    messageBody->value().Append(grpc_core::Slice(slice));
                }
                grpc_closure *c = messageClosure;
                messageClosure = nullptr;
                h->flags &= ~Wire::Header::messageComplete;
                grpc_core::ExecCtx::Run(DEBUG_LOCATION, c, absl::OkStatus());
                gpr_log(GPR_INFO, "Invoked message closure for stream id %d",
                        streamId.id);
            }
        }

        // Transfer trailing metadata, if possible. On the server side we
        // mustn't transfer the metadata until outgoing trailing metadata
        // has been sent (don't know why).
        if (h->flags & Wire::Header::trailMdPresent) {
            eof = true;
            if (trailMdClosure && (trailMdSent || !isServer)) {
                msg->deserializeMetadata(
                        sizeof(Wire::Header) + msg->initMdLength,
                        msg->trailMdLength, trailMd);
                logMetadata(trailMd, "Incoming trailing metadata");
                grpc_closure *c = trailMdClosure;
                trailMdClosure = nullptr;
                h->flags &= ~Wire::Header::trailMdPresent;
                gpr_log(GPR_INFO, "Invoked trailing metadata closure for "
                        "stream id %d", streamId.id);
                grpc_core::ExecCtx::Run(DEBUG_LOCATION, c, absl::OkStatus());
            }
        }

        if ((h->flags & (Wire::Header::initMdPresent
                | Wire::Header::trailMdPresent | Wire::Header::messageComplete))
                || (msg->bodyLength != 0)) {
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
        grpc_closure *c = messageClosure;
        messageClosure = nullptr;
        grpc_core::ExecCtx::Run(DEBUG_LOCATION, c, absl::OkStatus());
        gpr_log(GPR_INFO, "Invoked message closure (eof)");
    }
}

/**
 * Invoked on servers to add the peer address to initial metadata.
 * \param md
 *      The address of the stream's peer should get added here.
 */
void HomaStream::addPeerToMetadata(grpc_metadata_batch *md)
{
    struct grpc_resolved_address addr;
    static_assert(GRPC_MAX_SOCKADDR_SIZE >= sizeof(streamId.addr));
    memcpy(addr.addr, &streamId.addr, sizeof(streamId.addr));
    addr.len = sizeof(streamId.addr);
    absl::StatusOr<std::string> peerString = grpc_sockaddr_to_uri(&addr);
    md->Set(grpc_core::PeerString(),
                        grpc_core::Slice::FromCopiedString(peerString.value()));
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
        notifyError(absl::CancelledError());
        return;
    }

    if (msg->sequence < nextIncomingSequence) {
        goto duplicate;
    }

    if (incoming.empty()) {
        incoming.push_back(std::move(msg));
    } else {
        for (size_t i = incoming.size(); i > 0; i--) {
            if (incoming[i-1]->sequence == msg->sequence) {
                goto duplicate;
            }
            if (incoming[i-1]->sequence < msg->sequence) {
                incoming.emplace(incoming.begin()+i, std::move(msg));
                goto messageInserted;
            }
        }
        incoming.emplace(incoming.begin(), std::move(msg));
    }
messageInserted:
    transferData();
    return;

duplicate:
    gpr_log(GPR_ERROR, "Dropping duplicate message, stream id %d, sequence %d, "
            "nextIncomingSequence %d, homaId %lu", streamId.id,
            msg->sequence, nextIncomingSequence, homaId);
}

/**
 * This method is invoked when a fatal error occurs on a stream. It invokes
 * any callbacks present for the stream.
 * \param errorInfo
 *      Information about what went wrong. This method takes ownership
 *      of the error.
 */
void HomaStream::notifyError(grpc_error_handle errorInfo)
{
    error = errorInfo;
    gpr_log(GPR_INFO, "Recording error for stream id %u: %s",
        streamId.id, error.ToString().c_str());

    if (initMdClosure) {
        grpc_closure *c = initMdClosure;
        initMdClosure = nullptr;
        grpc_core::ExecCtx::Run(DEBUG_LOCATION, c, error);
    }
    if (messageClosure) {
        grpc_closure *c = messageClosure;
        messageClosure = nullptr;
        grpc_core::ExecCtx::Run(DEBUG_LOCATION, c, error);
    }
    if (trailMdClosure) {
        grpc_closure *c = trailMdClosure;
        trailMdClosure = nullptr;
        grpc_core::ExecCtx::Run(DEBUG_LOCATION, c, error);
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