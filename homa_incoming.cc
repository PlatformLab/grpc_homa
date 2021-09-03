#include <cstdarg>
#include <memory>

#include "homa.h"
#include "homa_incoming.h"

HomaIncoming::HomaIncoming()
    : refs()
    , sliceRefs(grpc_slice_refcount::Type::REGULAR, &refs,
            HomaIncoming::destroyer, this, nullptr)
    , homaId()
    , streamId()
    , length()
    , baseLength()
    , sequence()
    , initMdLength()
    , messageLength()
    , trailMdLength()
    , hdr()
    , initialPayload()
    , tail()
    , destroyCounter(nullptr)
    , maxStaticMdLength(200)
{
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
 * Read (the first part of) an incoming Homa request or response message.
 * \param fd
 *      File descriptor of Homa socket from which to read the message
 * \param flags
 *      Flags to pass to homa_recv, such as HOMA_RECV_RESPONSE.
 * \return
 *      The message that was just read (or nullptr if there was an
 *      error reading the message).
 */
HomaIncoming::UniquePtr HomaIncoming::read(int fd, int flags)
{
    UniquePtr msg(new HomaIncoming);
    msg->streamId.addrSize = sizeof(streamId.addr);
    ssize_t result = homa_recv(fd, &msg->hdr,
            sizeof(msg->hdr) + sizeof(msg->initialPayload),
            flags | HOMA_RECV_PARTIAL, msg->streamId.sockaddr(),
            &msg->streamId.addrSize, &msg->homaId, &msg->length);
    if (result < 0) {
        if (errno != EAGAIN) {
            gpr_log(GPR_ERROR, "Error in homa_recv: %s", strerror(errno));
        }
        return nullptr;
    }
    msg->baseLength = result;
    if (msg->baseLength < sizeof(msg->hdr)) {
        gpr_log(GPR_ERROR, "Homa message contained only %lu bytes "
                "(need %lu bytes for header)",
                msg->baseLength, sizeof(msg->hdr));
        return nullptr;
    }
    msg->streamId.id = ntohl(msg->hdr.streamId);
    msg->sequence = ntohl(msg->hdr.sequenceNum);
    msg->initMdLength = htonl(msg->hdr.initMdBytes);
    msg->messageLength = htonl(msg->hdr.messageBytes);
    msg->trailMdLength = htonl(msg->hdr.trailMdBytes);
    uint32_t expected = sizeof(msg->hdr) + msg->initMdLength
            + msg->messageLength + msg->trailMdLength;
    if (msg->length != expected) {
        gpr_log(GPR_ERROR, "Bad message length %lu (expected %u)",
                msg->length, expected);
        return nullptr;
    }
    if (msg->length > msg->baseLength) {
        // Must read the tail of the message.
        msg->tail.resize(msg->length - msg->baseLength);
        ssize_t tail_length = homa_recv(fd, msg->tail.data(),
                msg->length - msg->baseLength, flags, msg->streamId.sockaddr(),
                &msg->streamId.addrSize, &msg->homaId, nullptr);
        if (tail_length < 0) {
            gpr_log(GPR_ERROR, "Error in homa_recv for tail of id %lu: %s",
                    msg->homaId, strerror(errno));
            return nullptr;
        }
        if ((msg->baseLength + tail_length) != msg->length) {
            gpr_log(GPR_ERROR, "Tail of Homa message has wrong length: "
                    "expected %lu bytes, got %lu bytes",
                    msg->length - msg->baseLength,  tail_length);
            return nullptr;
        }
    }
    gpr_log(GPR_INFO, "Received Homa message from host 0x%x, port %d with "
            "id %d, sequence %d, Homa id %lu, length %lu, addr_size %lu",
            msg->streamId.ipv4Addr(), msg->streamId.port(), msg->streamId.id,
            htonl(msg->hdr.sequenceNum), msg->homaId, msg->length,
            msg->streamId.addrSize);
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
        memcpy(dst, base() + offset, chunk1Length);
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
        slice.data.refcounted.bytes = base() + offset;
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
    memcpy(slice.data.refcounted.bytes, base() + offset, baseBytes);
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
        int index = msgMd->index;
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
        if (index < GRPC_BATCH_CALLOUTS_COUNT) {
            elemData->key = grpc_static_slice_table()[index];
        } else {
            elemData->key = getStaticSlice(offset, keyLength, arena);
        }
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
 *      The remaining arguments come in groups of three, consisting of a
 *      char* key, a char* value, and an int callout index. The list is
 *      terminated by a nullptr key value.
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
        int index = va_arg(ap, int);
        size_t keyLength = strlen(key);
        size_t valueLength = strlen(value);
        size_t length = keyLength + valueLength + sizeof(Wire::Mdata);
        buffer.resize(length);
        Wire::Mdata *md = reinterpret_cast<Wire::Mdata*>(buffer.data());
        md->index = index;
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
            memcpy(base() + offset, src, chunkSize);
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