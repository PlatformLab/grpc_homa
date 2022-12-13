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
    : refs()
    , sliceRefs(grpc_slice_refcount::Type::REGULAR, &refs,
            HomaIncoming::destroyer, this, nullptr)
    , streamId()
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
    sliceRefs.Ref();
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
    : refs()
    , sliceRefs(grpc_slice_refcount::Type::REGULAR, &refs,
            HomaIncoming::destroyer, this, nullptr)
    , streamId(999)
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
    sliceRefs.Ref();
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
 * \param arg
 *      Incoming to destroy.
 */
void HomaIncoming::destroyer(void *arg)
{
    delete static_cast<HomaIncoming*>(arg);
}

/**
 * Read an incoming Homa request or response message. This method also
 * takes care of sending automatic responses for streaming RPCs, and for
 * discarding those responses.
 * \param sock
 *      Homa socket from which to read message.
 * \param flags
 *      Flags to pass to homa_recv, such as HOMA_RECV_RESPONSE.
 * \param homaId
 *      Homa's id for the received message is stored here. This is
 *      needed to provide that info to callers in cases where nullptr
 *      is returned (e.g. because the RPC failed). 0 means no id.
 * \param error
 *      Error information will be stored here, or GRPC_ERROR_NONE
 *      if there was no error. Caller must invoke GRPC_ERROR_UNREF.
 *
 * \return
 *      The message that was just read, or nullptr if no message could
 *      be read. If *error == GRPC_ERROR_NONE and the result is empty,
 *      it means no message was available. If *error != GRPC_ERROR_NONE,
 *      then there was an error reading a message. In this case, if
 *      *homaId is nonzero, it means that that particular RPC failed.
 */
HomaIncoming::UniquePtr HomaIncoming::read(HomaSocket *sock, int flags,
        uint64_t *homaId, grpc_error_handle *error)
{
    UniquePtr msg(new HomaIncoming(sock));
    ssize_t result;
    Wire::Header *hdr, aux;
    uint64_t startTime = TimeTrace::rdtsc();

    *error = GRPC_ERROR_NONE;

    // The following loop executes multiple times if it receives (and
    // discards) streaming responses.
    struct msghdr recvHdr;
    recvHdr.msg_name = &msg->streamId.addr;
    recvHdr.msg_namelen = sizeof(msg->streamId.addr);
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
        result = recvmsg(sock->getFd(), &recvHdr, 0);
        *homaId = msg->recvArgs.id;
        if (result < 0) {
            if (errno == EAGAIN) {
                return nullptr;
            }
            gpr_log(GPR_ERROR, "Error in recvmsg (homaId %lu): %s",
                    *homaId, strerror(errno));
            *error = GRPC_OS_ERROR(errno, "recvmsg");
            return nullptr;
        }
        msg->length = result;
        if (msg->length < sizeof(Wire::Header)) {
            gpr_log(GPR_ERROR, "Homa message contained only %lu bytes "
                    "(need %lu bytes for header)",
                    msg->length, sizeof(Wire::Header));
            *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                    "Incoming Homa message too short for header");
            return nullptr;
        }
        hdr = msg->get<Wire::Header>(0, &aux);
        if (!(hdr->flags & Wire::Header::emptyResponse)) {
            break;
        }
        gpr_log(GPR_INFO,
                "Discarding dummy response for homaId %lu, stream id %d",
                *homaId, ntohl(msg->hdr()->streamId));
//        gpr_log(GPR_INFO, "Received %u bpages from Homa in dummy response: %s",
//                msg->recvArgs.num_bpages, bpagesToString(&msg->recvArgs).c_str());
    }

    // We now have a message suitable for returning to the caller.
    msg->streamId.id = ntohl(hdr->streamId);
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
        *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                "Incoming Homa message length doesn't match header");
        return nullptr;
    }

    if (gpr_should_log(GPR_LOG_SEVERITY_INFO)) {
        gpr_log(GPR_INFO, "Received Homa message from %s, sequence %d "
                "with homaId %lu, %u initMd bytes, %u message bytes, "
                "%u trailMd bytes, flags 0x%x, bpage[0] %u",
                msg->streamId.toString().c_str(), msg->sequence, *homaId,
                msg->initMdLength, msg->bodyLength, msg->trailMdLength,
                msg->hdr()->flags, msg->recvArgs.bpage_offsets[0]>>HOMA_BPAGE_SHIFT);
    }
    TimeTrace::record(startTime, "HomaIncoming::read starting");
    tt("HomaIncoming::read returning");
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
 * Extract part a message as a gRPC slice.
 * \param offset
 *      Offset within the message of the first byte of the slice.
 * \param length
 *      Length of the slice (caller must ensure that offset and length
 *      are within the range of the message).
 * \param arena
 *      Used to allocate storage for data that can't be stored internally
 *      in the slice.
 * \return
 *      A slice containing the desired data, which uses memory that need
 *      not be freed explicitly (data will either be internal to the slice
 *      or in an arena).
 */
grpc_slice HomaIncoming::getStaticSlice(size_t offset, size_t length,
        grpc_core::Arena *arena)
{
    grpc_slice slice;

    if (length < sizeof(slice.data.inlined.bytes)) {
        slice.refcount = nullptr;
        slice.data.inlined.length = length;
        copyOut(GRPC_SLICE_START_PTR(slice), offset, length);
        return slice;
    }

    slice.refcount = &grpc_core::kNoopRefcount;
    slice.data.refcounted.length = length;
    slice.data.refcounted.bytes =
            static_cast<uint8_t*>(arena->Alloc(length));
    copyOut(slice.data.refcounted.bytes, offset, length);
    return slice;
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
grpc_slice HomaIncoming::getSlice(size_t offset, size_t length)
{
    grpc_slice slice;

    if (contiguous(offset) >= length) {
        slice.data.refcounted.bytes = get<uint8_t>(offset, nullptr);
        slice.data.refcounted.length = length;
        slice.refcount = &sliceRefs;
        slice.refcount->Ref();
        return slice;
    }

    // The desired range is not contiguous in the message; make a copy to
    // bring all of the data together in one place.

    slice = grpc_slice_malloc(length);
    copyOut(slice.data.refcounted.bytes, offset, length);
    return slice;
}

/**
 * Parse metadata from an incoming message.
 * \param offset
 *      Offset of first byte of metadata in message.
 * \param length
 *      Number of bytes of metadata at @offset.
 * \param batch
 *      Add key-value metadata pairs to this structure.
 * \param arena
 *      Use this for allocating any memory needed for metadata.
 */
void HomaIncoming::deserializeMetadata(size_t offset, size_t length,
        grpc_metadata_batch* batch, grpc_core::Arena* arena)
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

        // This must be byte-compatible with grpc_mdelem_data. Need this
        // hack in order to avoid high overheads (e.g. extra memory allocation)
        // of the builtin mechanisms for creating metadata.
        struct ElemData {
            grpc_slice key;
            grpc_slice value;
            ElemData() : key(), value() {}
        };

        ElemData *elemData = arena->New<ElemData>();
        grpc_linked_mdelem* lm = arena->New<grpc_linked_mdelem>();

        // The contortion below is needed to make sure that StaticMetadataSlices
        // are generated for keys when appropriate.
        grpc_slice tmpSlice = getStaticSlice(offset, keyLength, arena);
        elemData->key = grpc_core::ManagedMemorySlice(&tmpSlice);

        if (valueLength <= maxStaticMdLength) {
            elemData->value = getStaticSlice(offset+keyLength, valueLength,
                    arena);
            lm->md = GRPC_MAKE_MDELEM(elemData, GRPC_MDELEM_STORAGE_EXTERNAL);
        } else {
            lm->md = grpc_mdelem_from_slices(elemData->key,
                    getSlice(offset + keyLength, valueLength));
        }

        grpc_error_handle error = grpc_metadata_batch_link_tail(batch, lm);
        if (error != GRPC_ERROR_NONE) {
            gpr_log(GPR_ERROR, "Error creating metadata for %.*s: %s",
                    static_cast<int>(GRPC_SLICE_LENGTH(elemData->key)),
                    GRPC_SLICE_START_PTR(elemData->key),
                    grpc_error_string(error));
            GPR_ASSERT(error == GRPC_ERROR_NONE);
        }
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
