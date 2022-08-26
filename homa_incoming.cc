#include <cstdarg>
#include <memory>

#include "homa.h"
#include "homa_incoming.h"
#include "time_trace.h"
#include "util.h"

HomaIncoming::HomaIncoming()
    : refs()
    , sliceRefs(grpc_slice_refcount::Type::REGULAR, &refs,
            HomaIncoming::destroyer, this, nullptr)
    , streamId()
    , length()
    , baseLength()
    , sequence()
    , initMdLength()
    , messageLength()
    , trailMdLength()
    , initialPayload()
    , tail()
    , destroyCounter(nullptr)
    , maxStaticMdLength(200)
{
}

/**
 * This constructor is intended primarily for testing. It generates a
 * message that includes dummy metadata and message data as specified
 * by the arguments. This constructor assumes that the message fits
 * entirely within initialPayload.
 * \param sequence
 *      Sequence number for the message.
 * \param initMd
 *      If true, some initial metadata will be present in the message.
 * \param messageLength
 *      Indicates how much dummy message data to include in the @initialPayload
 *      part of the message.
 * \param tailLength
 *      How much dummy data to store in the message tail.
 * \param firstValue
 *      Passed to Mock:fillData to determine the contents of message data.
 * \param messageComplete
 *      Value for the messageComplete flag in the message header.
 * \param trailMd
 *      If true, some trailing metadata will be present in the message.
 */
HomaIncoming::HomaIncoming(int sequence, bool initMd, size_t messageLength,
        size_t tailLength, int firstValue, bool messageComplete,
        bool trailMd)
    : refs()
    , sliceRefs(grpc_slice_refcount::Type::REGULAR, &refs,
            HomaIncoming::destroyer, this, nullptr)
    , streamId(999)
    , length(0)
    , baseLength(sizeof(Wire::Header))
    , sequence(sequence)
    , initMdLength(0)
    , messageLength(messageLength)
    , trailMdLength(0)
    , initialPayload()
    , tail()
    , destroyCounter(nullptr)
    , maxStaticMdLength(200)
{
    size_t offset = sizeof(Wire::Header);
    size_t length;
    
    if (initMd) {
        length = addMetadata(offset, sizeof(initialPayload),
                "initMd1", "value1", ":path", "/x/y",
                nullptr);
        initMdLength = length;
        hdr()->initMdBytes = htonl(length);
        hdr()->flags |= Wire::Header::initMdPresent;
        offset += length;
        baseLength += length;
    }
    
    if (trailMd) {
        length = addMetadata(offset, sizeof(initialPayload),
                "k2", "0123456789", nullptr);
        trailMdLength = length;
        hdr()->trailMdBytes = htonl(length);
        hdr()->flags |= Wire::Header::trailMdPresent;
        offset += length;
        baseLength += length;
    }
    
    if (messageLength > 0) {
        fillData(initialPayload + offset, messageLength, firstValue);
        hdr()->messageBytes = htonl(messageLength);
        baseLength += messageLength;
    }
    if (tailLength > 0) {
        tail.resize(tailLength);
        fillData(tail.data(), tailLength, firstValue + messageLength);
    }
    hdr()->sequenceNum = htonl(sequence);
    hdr()->messageBytes = htonl(messageLength + tailLength);
    this->messageLength = messageLength + tailLength;
    if (messageComplete) {
        hdr()->flags |= Wire::Header::messageComplete;
    }
}

HomaIncoming::~HomaIncoming()
{
    if (destroyCounter) {
        (*destroyCounter)++;
    }
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
 * \param fd
 *      File descriptor of Homa socket from which to read the message
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
 *      The message that was just read, or empty if no message could
 *      be read. If *error == GRPC_ERROR_NONE and the result is empty,
 *      it means no message was available. If *error != GRPC_ERROR_NONE,
 *      then there was an error reading a message. In this case, if
 *      *homaId is nonzero, it means that that particular RPC failed.
 */
HomaIncoming::UniquePtr HomaIncoming::read(int fd, int flags,
        uint64_t *homaId, grpc_error_handle *error, uint64_t* cookie)
{
    UniquePtr msg(new HomaIncoming);
    ssize_t result;
    uint64_t startTime = TimeTrace::rdtsc();
    
    *error = GRPC_ERROR_NONE;
    
    // The following loop executes multiple times if it receives (and
    // discards) streaming responses.
    while (true) {
        msg->streamId.addrSize = sizeof(streamId.addr);
        *homaId = 0;
        result = homa_recv(fd, &msg->initialPayload,
                sizeof(msg->initialPayload),
                flags | HOMA_RECV_PARTIAL, msg->streamId.sockaddr(),
                &msg->streamId.addrSize, homaId, &msg->length, cookie);
        if (result < 0) {
            if (errno == EAGAIN) {
                return nullptr;
            }
            gpr_log(GPR_DEBUG, "Error in homa_recv (homaId %lu): %s",
                    *homaId, strerror(errno));
            *error = GRPC_OS_ERROR(errno, "homa_recv");
            return nullptr;
        }
        if (!(msg->hdr()->flags & Wire::Header::emptyResponse)) {
            break;
        }
        gpr_log(GPR_INFO,
                "Discarding dummy response for homaId %lu, stream id %d",
                *homaId, ntohl(msg->hdr()->streamId));
    }
    
    // We now have a message suitable for returning to the caller.
    msg->baseLength = result;
    if (msg->baseLength < sizeof(Wire::Header)) {
        gpr_log(GPR_ERROR, "Homa message contained only %lu bytes "
                "(need %lu bytes for header)",
                msg->baseLength, sizeof(Wire::Header));
        *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                "Incoming Homa message too short for header");
        return nullptr;
    }
    msg->streamId.id = ntohl(msg->hdr()->streamId);
    msg->sequence = ntohl(msg->hdr()->sequenceNum);
    msg->initMdLength = ntohl(msg->hdr()->initMdBytes);
    msg->messageLength = ntohl(msg->hdr()->messageBytes);
    msg->trailMdLength = ntohl(msg->hdr()->trailMdBytes);
    uint32_t expected = sizeof(Wire::Header) + msg->initMdLength
            + msg->messageLength + msg->trailMdLength;
    if (msg->length != expected) {
        gpr_log(GPR_ERROR, "Bad message length %lu (expected %u); "
                "initMdLength %d, messageLength %d, trailMdLength %d,"
                "header length %lu",
                msg->length, expected, msg->initMdLength, msg->messageLength,
                msg->trailMdLength, sizeof(Wire::Header));
        *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                "Incoming Homa message length doesn't match header");
        return nullptr;
    }
    
    if (msg->length > msg->baseLength) {
        // Must read the tail of the message.
        msg->tail.resize(msg->length - msg->baseLength);
        ssize_t tail_length = homa_recv(fd, msg->tail.data(),
                msg->length - msg->baseLength, flags, msg->streamId.sockaddr(),
                &msg->streamId.addrSize, homaId, nullptr, cookie);
        if (tail_length < 0) {
            gpr_log(GPR_ERROR, "Error in homa_recv for tail of id %lu: %s",
                    *homaId, strerror(errno));
            *error = GRPC_OS_ERROR(errno, "homa_recv");
            return nullptr;
        }
        if ((msg->baseLength + tail_length) != msg->length) {
            gpr_log(GPR_ERROR, "Tail of Homa message has wrong length: "
                    "expected %lu bytes, got %lu bytes",
                    msg->length - msg->baseLength,  tail_length);
            *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                    "Tail of Homa message length has wrong length");
            return nullptr;
        }
    }
    gpr_log(GPR_INFO, "Received Homa message from host 0x%x, port %d with "
            "homaId %lu, stream id %d, sequence %d, %u initMd bytes, "
            "%u message bytes, %u trailMd bytes, flags 0x%x",
            msg->streamId.ipv4Addr(), msg->streamId.port(), *homaId,
            msg->streamId.id, msg->sequence, msg->initMdLength,
            msg->messageLength, msg->trailMdLength, msg->hdr()->flags);
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
    size_t chunk2Length = length;
    size_t offset2;
    uint8_t *dst2 = static_cast<uint8_t *>(dst);
    if (baseLength > offset) {
        size_t chunk1Length = baseLength - offset;
        if (chunk1Length > length) {
            chunk1Length = length;
        }
        memcpy(dst, initialPayload + offset, chunk1Length);
        dst2 += chunk1Length;
        chunk2Length -= chunk1Length;
        offset2 = 0;
    } else {
        offset2 = offset - baseLength;
    }
    
    if (chunk2Length > 0) {
        memcpy(dst2, tail.data() + offset2, chunk2Length);
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
 * Extract part a message as a gRPC slice.
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
    
    // See if the desired range lies entirely within the first part
    // of the message.
    if ((offset + length) <= baseLength) {
        slice.data.refcounted.bytes = initialPayload + offset;
        slice.data.refcounted.length = length;
        slice.refcount = &sliceRefs;
        slice.refcount->Ref();
        return slice;
    }
    
    // See if the desired range lies entirely within the tail.
    if (offset >= baseLength) {
        slice.data.refcounted.bytes = tail.data() + (offset - baseLength);
        slice.data.refcounted.length = length;
        slice.refcount = &sliceRefs;
        slice.refcount->Ref();
        return slice;
    }
    
    // The desired range straddles the two halves; make a copy to bring
    // all of the data together in one place.

    slice = grpc_slice_malloc(length);
    size_t baseBytes = baseLength - offset;
    memcpy(slice.data.refcounted.bytes, initialPayload + offset, baseBytes);
    memcpy(slice.data.refcounted.bytes + baseBytes, tail.data(),
            length - baseBytes);
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
        
        msgMd = getBytes<Wire::Mdata>(offset, &metadataBuffer);
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
 * Add metadata information to the message; this method is intended
 * primarily for unit testing.
 * \param offset
 *      Offset within the message at which to start writing metadata.
 * \param staticLength
 *      How many bytes of metadata to store in @payload; the remainder
 *      will go in the tail (an entry may be split across the boundary).
 * \param ...
 *      The remaining arguments come in groups of two, consisting of a
 *      char* key and a char* value. The list is terminated by a nullptr
 *      key value.
 * \return
 *      The total number of bytes of new metadata added.
 */
size_t HomaIncoming::addMetadata(size_t offset, size_t staticLength, ...)
{
    va_list ap;
    va_start(ap, staticLength);
    std::vector<uint8_t> buffer;
    size_t newData = 0;
    baseLength = staticLength;
    while (true) {
        // First, create a metadata element in contiguous memory.
        char *key = va_arg(ap, char*);
        if (key == nullptr) {
            break;
        }
        char *value = va_arg(ap, char*);
        size_t keyLength = strlen(key);
        size_t valueLength = strlen(value);
        size_t length = keyLength + valueLength + sizeof(Wire::Mdata);
        buffer.resize(length);
        Wire::Mdata *md = reinterpret_cast<Wire::Mdata*>(buffer.data());
        md->keyLength = htonl(keyLength);
        md->valueLength = htonl(valueLength);
        memcpy(md->data, key, keyLength);
        memcpy(md->data+keyLength, value, valueLength);
        newData += length;
        
        // Now copy the element into the message (possibly in 2 chunks).
        uint8_t *src = buffer.data();
        size_t tailOffset = 0;
        size_t tailLength = length;
        if (offset < staticLength) {
            size_t chunkSize = staticLength - offset;
            if (chunkSize > length) {
                chunkSize = length;
            }
            memcpy(initialPayload + offset, src, chunkSize);
            tailLength -= chunkSize;
            src += chunkSize;
        } else {
            tailOffset = offset - staticLength;
        }
        if (tailLength > 0) {
            tail.resize(tailOffset + tailLength);
            memcpy(tail.data() + tailOffset, src, tailLength);
        }
        offset += length;
    }
	va_end(ap);
    return newData;
}
