#include <memory>

#include "homa.h"
#include "homa_incoming.h"

/**
 * Constructor for Incoming.
 */
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
    , tail()
    , hdr()
    , initialPayload()
{
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
    msg->baseLength = homa_recv(fd, &msg->hdr,
            sizeof(msg->hdr) + sizeof(msg->initialPayload),
            flags | HOMA_RECV_PARTIAL, msg->streamId.sockaddr(),
            &msg->streamId.addrSize, &msg->homaId, &msg->length);
    
    msg->streamId.id = ntohl(msg->hdr.streamId);
    msg->sequence = ntohl(msg->hdr.sequenceNum);
    msg->initMdLength = htonl(msg->hdr.initMdBytes);
    msg->messageLength = htonl(msg->hdr.messageBytes);
    msg->trailMdLength = htonl(msg->hdr.trailMdBytes);
    uint32_t expected = sizeof(msg->hdr) + msg->initMdLength
            + msg->messageLength + msg->trailMdLength;
    if (msg->baseLength < 0) {
        if (errno != EAGAIN) {
            gpr_log(GPR_ERROR, "Error in homa_recv for id %lu: %s",
                    msg->homaId, strerror(errno));
        }
        return nullptr;
    } else if (msg->baseLength < static_cast<int>(sizeof(msg->hdr))) {
        gpr_log(GPR_ERROR, "Homa message contained only %lu bytes "
                "(need %lu bytes for header)", msg->baseLength, sizeof(msg->hdr));
        return nullptr;
    } else if (msg->length != expected) {
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
            gpr_log(GPR_ERROR, "Tail of Homa message too short: expected "
                    "%lu bytes, got %lu bytes", msg->length - msg->baseLength,
                    tail_length);
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
 * Extract part a message as a gRPC slice.
 * \param offset
 *      Offset within the message of the first byte of the slice.
 * \param length
 *      Length of the slice (caller must ensure that offset and length
 *      are within the range of the message).
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
    memcpy(slice.data.inlined.bytes, base() + offset, baseBytes);
    memcpy(slice.data.inlined.bytes + baseBytes, tail.data() + baseBytes,
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
        gpr_log(GPR_INFO, "Incoming metadata: index %d, key %.*s, value %.*s",
                msgMd->index, keyLength, msgMd->data, valueLength,
                msgMd->data+keyLength);
        
        grpc_slice keySlice;
        if (index < GRPC_BATCH_CALLOUTS_COUNT) {
            keySlice = grpc_static_slice_table()[index];
        } else {
            keySlice = getSlice(offset, keyLength);
        }
        grpc_linked_mdelem* lm = arena->New<grpc_linked_mdelem>();
        lm->md = grpc_mdelem_from_slices(keySlice,
                getSlice(offset + keyLength, valueLength));
        lm->md = GRPC_MAKE_MDELEM(GRPC_MDELEM_DATA(lm->md),
                GRPC_MDELEM_STORAGE_EXTERNAL);
        grpc_error_handle error = grpc_metadata_batch_link_tail(batch, lm);
        if (error != GRPC_ERROR_NONE) {
            gpr_log(GPR_INFO, "Error creating metadata for %.*s: %s",
                    keyLength, keySlice.data.inlined.bytes,
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