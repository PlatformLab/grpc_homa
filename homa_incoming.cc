#include <cstdarg>
#include <memory>
#include <thread>

#include "homa_incoming.h"
#include "homa_socket.h"
#include "time_trace.h"
#include "util.h"

/**
 * Constructor for HomaIncoming objects.
 * \param sock
 *      Socket from which to read messages.
 */
HomaIncoming::HomaIncoming(HomaSocket *sock)
    : sliceRefs(HomaIncoming::destroyer)
    , sock(sock)
    , recvArgs()
    , length()
    , sequence()
    , initMdLength()
    , bodyLength()
    , trailMdLength()
    , destroyCounter(nullptr)
    , maxStaticMdLength(200)
{
    memset(&recvArgs, 0, sizeof(recvArgs));
}

/**
 * This constructor is intended primarily for testing. It generates a
 * message that includes dummy metadata and message data as specified
 * by the arguments.
 * \param sock
 *      Info about Homa socket; typically (i.e., for tests) only bufRegion
 *      is used.
 * \param sequence
 *      Sequence number for the message.
 * \param initMd
 *      If true, some initial metadata will be present in the message.
 * \param bodyLength
 *      Indicates how much dummy message data to include in the message.
 * \param firstValue
 *      Passed to Mock:fillData to determine the contents of message data.
 * \param messageComplete
 *      Value for the messageComplete flag in the message header.
 * \param trailMd
 *      If true, some trailing metadata will be present in the message.
 */
HomaIncoming::HomaIncoming(HomaSocket *sock, int sequence, bool initMd,
        size_t bodyLength, int firstValue, bool messageComplete,
        bool trailMd)
    : sliceRefs(HomaIncoming::destroyer)
    , sock(sock)
    , recvArgs()
    , length(0)
    , sequence(sequence)
    , initMdLength(0)
    , bodyLength(bodyLength)
    , trailMdLength(0)
    , destroyCounter(nullptr)
    , maxStaticMdLength(200)
{
    memset(&recvArgs, 0, sizeof(recvArgs));
    recvArgs.num_bpages = 1;
    recvArgs.bpage_offsets[0] = 1000;
    uint8_t *buffer = sock->getBufRegion() + recvArgs.bpage_offsets[0];
    Wire::Header *h = new (buffer) Wire::Header;
    size_t offset = sizeof(Wire::Header);
    size_t length;

    // This is needed so that addMetadata can call getBytes. The value
    // gets corrected later.
    this->length = 10000;

    if (initMd) {
        length = addMetadata(offset, "initMd1", "value1", ":path", "/x/y",
                nullptr);
        initMdLength = length;
        h->initMdBytes = htonl(length);
        h->flags |= Wire::Header::initMdPresent;
        offset += length;
    }

    if (trailMd) {
        length = addMetadata(offset, "k2", "0123456789", nullptr);
        trailMdLength = length;
        h->trailMdBytes = htonl(length);
        h->flags |= Wire::Header::trailMdPresent;
        offset += length;
    }

    if (bodyLength > 0) {
        fillData(buffer + offset, bodyLength, firstValue);
        h->messageBytes = htonl(bodyLength);
        offset += bodyLength;
    }
    h->sequenceNum = htonl(sequence);
    h->messageBytes = htonl(bodyLength);
    this->bodyLength = bodyLength;
    this->length = offset;
    if (messageComplete) {
        h->flags |= Wire::Header::messageComplete;
    }
}

HomaIncoming::~HomaIncoming()
{
    if (destroyCounter) {
        (*destroyCounter)++;
    }
    sock->saveBuffers(&recvArgs);
}

/**
 * This method is invoked when sliceRefs for an Incoming becomes zero;
 * it frees the Incoming.
 * \param sliceRefs
 *      Pointer to sliceRefs field in the Incoming to destroy.
 */
void HomaIncoming::destroyer(grpc_slice_refcount *sliceRefs)
{
    HomaIncoming *incoming = containerOf(sliceRefs, &HomaIncoming::sliceRefs);
    delete incoming;
}

/**
 * Read an incoming Homa request or response message. This method also
 * takes care of sending automatic responses for streaming RPCs, and for
 * discarding those responses.
 * \param sock
 *      Homa socket from which to read message.
 * \param flags
 *      Flags to pass to homa_recv, such as HOMA_RECV_RESPONSE.
 * \param results
 *      Used to return multiple values to the caller.
 *
 * \return
 *      The message that was just read, or nullptr if no message could
 *      be read. Additional information is returned in @results. If
 *      results->error.ok() and the return value is empty, it means no message
 *      was available.
 */
HomaIncoming::UniquePtr HomaIncoming::read(HomaSocket *sock, int flags,
        ReadResults *results)
{
    UniquePtr msg(new HomaIncoming(sock));
    ssize_t status;
    Wire::Header *hdr, aux;
    uint64_t startTime = TimeTrace::rdtsc();

    results->error = absl::OkStatus();

    // The following loop executes multiple times if it receives (and
    // discards) streaming responses.
    struct msghdr recvHdr;
    recvHdr.msg_name = &results->streamId.addr;
    recvHdr.msg_namelen = sizeof(results->streamId.addr);
    recvHdr.msg_iov = nullptr;
    recvHdr.msg_iovlen = 0;
    recvHdr.msg_control = &msg->recvArgs;
    recvHdr.msg_controllen = sizeof(msg->recvArgs);
    msg->recvArgs.flags = flags;
    msg->recvArgs.num_bpages = 0;
    while (true) {
        msg->recvArgs.id = 0;
        sock->getSavedBuffers(&msg->recvArgs);
//        gpr_log(GPR_INFO, "Returning %d bpages to Homa: %s",
//               msg->recvArgs.num_bpages, bpagesToString(&msg->recvArgs).c_str());
        startTime = TimeTrace::rdtsc();
        status = recvmsg(sock->getFd(), &recvHdr, 0);
        results->homaId = msg->recvArgs.id;
        if (status < 0) {
            results->streamId.addr = msg->recvArgs.peer_addr;
            results->streamId.id = msg->recvArgs.completion_cookie;
            if (errno == EAGAIN) {
                return nullptr;
            }
            gpr_log(GPR_ERROR, "Error in recvmsg (homaId %lu): %s",
                    results->homaId, strerror(errno));
            results->error = GRPC_OS_ERROR(errno, "recvmsg");
            return nullptr;
        }
        TimeTrace::record(startTime, "HomaIncoming::read invoking recvmsg");
        tt("recvmsg returned");
        msg->length = status;
        if (msg->length < sizeof(Wire::Header)) {
            gpr_log(GPR_ERROR, "Homa message contained only %lu bytes "
                    "(need %lu bytes for header)",
                    msg->length, sizeof(Wire::Header));
            results->error = GRPC_ERROR_CREATE(
                    "Incoming Homa message too short for header");
            return nullptr;
        }
        hdr = msg->get<Wire::Header>(0, &aux);
        if (!(hdr->flags & Wire::Header::emptyResponse)) {
            break;
        }
        gpr_log(GPR_INFO,
                "Discarding dummy response for homaId %lu, stream id %d",
                results->homaId, ntohl(msg->hdr()->streamId));
//        gpr_log(GPR_INFO, "Received %u bpages from Homa in dummy response: %s",
//                msg->recvArgs.num_bpages, bpagesToString(&msg->recvArgs).c_str());
    }

    // We now have a message suitable for returning to the caller.
    results->streamId.id = ntohl(hdr->streamId);
    msg->sequence = ntohl(hdr->sequenceNum);
    msg->initMdLength = ntohl(hdr->initMdBytes);
    msg->bodyLength = ntohl(hdr->messageBytes);
    msg->trailMdLength = ntohl(hdr->trailMdBytes);
    uint32_t expected = sizeof(Wire::Header) + msg->initMdLength
            + msg->bodyLength + msg->trailMdLength;
    if (msg->length != expected) {
        gpr_log(GPR_ERROR, "Bad message length %lu (expected %u); "
                "initMdLength %d, bodyLength %d, trailMdLength %d,"
                "header length %lu",
                msg->length, expected, msg->initMdLength, msg->bodyLength,
                msg->trailMdLength, sizeof(Wire::Header));
        results->error = GRPC_ERROR_CREATE(
                "Incoming Homa message length doesn't match header");
        return nullptr;
    }

    if (gpr_should_log(GPR_LOG_SEVERITY_INFO)) {
        gpr_log(GPR_INFO, "Received Homa message from %s, sequence %d "
                "with homaId %lu, %u initMd bytes, %u message bytes, "
                "%u trailMd bytes, flags 0x%x, bpage[0] %u",
                results->streamId.toString().c_str(), msg->sequence,
                results->homaId, msg->initMdLength, msg->bodyLength,
                msg->trailMdLength, msg->hdr()->flags,
                msg->recvArgs.bpage_offsets[0]>>HOMA_BPAGE_SHIFT);
    }
    return msg;
}

/**
 * Copy a range of bytes from an incoming message to a contiguous
 * external block.
 * \param dst
 *      Where to copy the data.
 * \param offset
 *      Offset within the message of the first byte of data to copy.
 * \param length
 *      Number of bytes to copy.
 */
void HomaIncoming::copyOut(void *dst, size_t offset, size_t length)
{
    uint8_t *dst2 = static_cast<uint8_t *>(dst);

    while (length > 0) {
        size_t chunkLength = contiguous(offset);
        if (chunkLength == 0) {
            break;
        }
        if (chunkLength > length) {
            chunkLength = length;
        }
        memcpy(dst2, get<char>(offset, nullptr), chunkLength);
        offset += chunkLength;
        dst2 += chunkLength;
        length -= chunkLength;
    }
}

/**
 * Extract part a message as a single gRPC slice.
 * \param offset
 *      Offset within the message of the first byte of the slice.
 * \param length
 *      Length of the slice (caller must ensure that offset and length
 *      are within the range of the message).
 * \result
 *      A slice containing the desired data. The slice is managed (e.g.
 *      it has a non-null reference count that must be used to eventually
 *      release the slice's data).
 */
grpc_core::Slice HomaIncoming::getSlice(size_t offset, size_t length)
{
    grpc_slice slice;

    if (contiguous(offset) >= length) {
        slice.data.refcounted.bytes = get<uint8_t>(offset, nullptr);
        slice.data.refcounted.length = length;
        slice.refcount = &sliceRefs;
        slice.refcount->Ref({});
        return grpc_core::Slice(slice);
    }

    // The desired range is not contiguous in the message; make a copy to
    // bring all of the data together in one place.

    slice = grpc_slice_malloc(length);
    copyOut(slice.data.refcounted.bytes, offset, length);
    return grpc_core::Slice(slice);
}

/**
 * Parse metadata from an incoming message.
 * \param offset
 *      Offset of first byte of metadata in message.
 * \param length
 *      Number of bytes of metadata at @offset.
 * \param batch
 *      Add key-value metadata pairs to this structure.
 */
void HomaIncoming::deserializeMetadata(size_t offset, size_t length,
        grpc_metadata_batch* batch)
{
    size_t remaining = length;

    // Each iteration through this loop extracts one metadata item and
    // adds it to @batch.
    while (remaining > sizeof(Wire::Mdata)) {
        Wire::Mdata metadataBuffer;
        Wire::Mdata* msgMd;

        msgMd = get<Wire::Mdata>(offset, &metadataBuffer);
        uint32_t keyLength = ntohl(msgMd->keyLength);
        uint32_t valueLength = ntohl(msgMd->valueLength);
        remaining -= sizeof(*msgMd);
        offset += sizeof(*msgMd);
        if ((keyLength + valueLength) > remaining) {
            gpr_log(GPR_ERROR, "Metadata format error: key (%u bytes) and "
                    "value (%u bytes) exceed remaining space (%lu bytes)",
                    keyLength, valueLength, remaining);
            return;
        }
        char key[keyLength];
        copyOut(key, offset, keyLength);
        auto md = grpc_metadata_batch::Parse(absl::string_view(key, keyLength),
                getSlice(offset+keyLength, valueLength), false,
                keyLength + valueLength,
                [keyLength, &key] (absl::string_view error,
                        const grpc_core::Slice& value) -> void {
                    int msgLength = error.length();
                    gpr_log(GPR_ERROR, "Error parsing value for incoming "
                            "metadata %.*s: %.*s; ignoring this item",
                            keyLength, key, msgLength, error.data());
                });
        batch->Set(std::move(md));
        remaining -= keyLength + valueLength;
        offset += keyLength + valueLength;
    }
    if (remaining != 0) {
        gpr_log(GPR_ERROR, "Metadata format error: need %lu bytes for "
                "metadata descriptor, but only %lu bytes available",
                sizeof(Wire::Mdata), remaining);
    }
}

/**
 * Add metadata information to a message; this method is intended
 * primarily for unit testing.
 * \param offset
 *      Offset within the message at which to start writing metadata.
 *      There must be enough contiguous space in the message to hold all
 *      of the metadata.
 * \param ...
 *      The remaining arguments come in groups of two, consisting of a
 *      char* key and a char* value. The list is terminated by a nullptr
 *      key value.
 * \return
 *      The total number of bytes of serialized metadata that were generated.
 */
size_t HomaIncoming::addMetadata(size_t offset, ...)
{
    va_list ap;
    va_start(ap, offset);
    size_t newData = 0;
    while (true) {
        char *key = va_arg(ap, char*);
        if (key == nullptr) {
            break;
        }
        char *value = va_arg(ap, char*);
        size_t keyLength = strlen(key);
        size_t valueLength = strlen(value);
        size_t length = keyLength + valueLength + sizeof(Wire::Mdata);
        Wire::Mdata *md = get<Wire::Mdata>(offset, nullptr);
        md->keyLength = htonl(keyLength);
        md->valueLength = htonl(valueLength);
        memcpy(md->data, key, keyLength);
        memcpy(md->data+keyLength, value, valueLength);
        offset += length;
        newData += length;
    }
	va_end(ap);
    return newData;
}
