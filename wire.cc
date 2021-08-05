#include <arpa/inet.h>
#include <string.h>

#include <optional>
#include <string>

#include "util.h"
#include "wire.h"

grpc_core::StaticSliceRefcount *Wire::calloutRefs[GRPC_BATCH_CALLOUTS_COUNT];

/**
 * Invoked once to initialize static information for this class.
 */
void Wire::init()
{
    if (calloutRefs[0] != nullptr) {
        return;
    }
    for (int i = 0; i < GRPC_BATCH_CALLOUTS_COUNT; i++) {
        calloutRefs[i] = new grpc_core::StaticSliceRefcount(i);
    }
}

/**
 * Serialize a batch of metadata and append it to a message buffer.
 * \param batch
 *      All of the key-value pairs here will be appended.
 * \param dest
 *      Where to store the serialized data (presumably a message buffer?).
 * \return
 *      The total number of bytes appended at dest.
 */
size_t Wire::serializeMetadata(grpc_metadata_batch* batch, uint8_t *dest)
{
    // For each metadata item, two string values are appended to dest (first
    // key, then value). Each value consists of a 4-byte length (in network
    // byte order) followed by that many bytes of key or value data.
    uint8_t *cur = dest;
    for (grpc_linked_mdelem* md = batch->list.head; md != nullptr;
            md = md->next) {
        Wire::Mdata* msgMd = reinterpret_cast<Wire::Mdata*>(cur);
        const grpc_slice& key = GRPC_MDKEY(md->md);
        uint32_t keyLength = GRPC_SLICE_LENGTH(key);
        int index = GRPC_BATCH_INDEX_OF(key);
        const grpc_slice& value = GRPC_MDVALUE(md->md);
        uint32_t valueLength = GRPC_SLICE_LENGTH(value);
        
        msgMd->index = index;
        msgMd->keyLength = htonl(keyLength);
        msgMd->valueLength = htonl(valueLength);
        cur += sizeof(*msgMd);
        memcpy(cur, GRPC_SLICE_START_PTR(key), keyLength);
        cur += keyLength;
        memcpy(cur, GRPC_SLICE_START_PTR(value), valueLength);
        cur += valueLength;
    }
    return cur - dest;
}

/**
 * Parse metadata from an incoming message.
 * \param src
 *      Pointer to first byte of metadata in message. Note: this data
 *      must persist for the lifetime of @batch.
 * \param length
 *      Number of bytes of metadata at @src.
 * \param batch
 *      Add key-value metadata pairs to this structure.
 * \param arena
 *      Use this for allocating any memory needed for metadata.
 */
void Wire::deserializeMetadata(uint8_t *src, size_t length,
        grpc_metadata_batch* batch, grpc_core::Arena* arena)
{
    size_t remaining = length;
    // Generation through this loop extracts one metadata item and
    // adds it to @batch.
    while (remaining > sizeof(Mdata)) {
        Wire::Mdata* msgMd = reinterpret_cast<Wire::Mdata*>(src);
        int index = msgMd->index;
        uint32_t keyLength = ntohl(msgMd->keyLength);
        uint32_t valueLength = ntohl(msgMd->valueLength);
        remaining -= sizeof(*msgMd);
        src += sizeof(*msgMd);
        GPR_ASSERT(remaining >= (keyLength + valueLength));
        
        grpc_linked_mdelem* lm = arena->New<grpc_linked_mdelem>();
        grpc_slice_refcount* ref = &grpc_core::kNoopRefcount;
        if (index < GRPC_BATCH_CALLOUTS_COUNT) {
            ref = &calloutRefs[index]->base;
        }
        grpc_core::StaticMetadataSlice keySlice(ref, keyLength, src);
        grpc_core::ExternallyManagedSlice valueSlice(src+keyLength, valueLength);
        lm->md = grpc_mdelem_from_slices(keySlice,
                *(static_cast<grpc_slice *>(&valueSlice)));
        lm->md = GRPC_MAKE_MDELEM(GRPC_MDELEM_DATA(lm->md),
                GRPC_MDELEM_STORAGE_EXTERNAL);
        grpc_error_handle error = grpc_metadata_batch_link_tail(batch, lm);
        if (error != GRPC_ERROR_NONE) {
            gpr_log(GPR_INFO, "Error creating metatata for %.*s: %s",
                    keyLength, src, grpc_error_string(error));
            GPR_ASSERT(error == GRPC_ERROR_NONE);
        }
        remaining -= keyLength + valueLength;
        src += keyLength + valueLength;
    }
    GPR_ASSERT(remaining == 0);
}

/**
 * Print to the log the contents of a block of metadata, as serialized
 * by appendMetadata.
 * \param buffer
 *      First byte of serialized data.
 * \param length
 *      Total amount of serialized data.
 */
void Wire::dumpMetadata(uint8_t *buffer, size_t length)
{
    size_t remaining = length;
    uint8_t *src = buffer;
    
    // Each iteration prints on metadata value
    while (remaining > 0) {
        Wire::Mdata* msgMd = reinterpret_cast<Wire::Mdata*>(src);
        if (remaining < sizeof(*msgMd)) {
             gpr_log(GPR_INFO, "Not enough bytes for meatadata header: "
                    "need %lu, have %lu", sizeof(*msgMd), remaining);
             return;
        }
        int index = msgMd->index;
        uint32_t keyLength = ntohl(msgMd->keyLength);
        uint32_t valueLength = ntohl(msgMd->valueLength);
        remaining -= sizeof(*msgMd);
        src += sizeof(*msgMd);
        if (remaining < (keyLength + valueLength)) {
             gpr_log(GPR_INFO, "Not enough bytes for key and value: need %u, "
                    "have %lu",keyLength + valueLength, remaining);
             return;
        }
        gpr_log(GPR_INFO, "Key: %.*s, value: %.*s, index: %d", keyLength, src,
                valueLength, src+keyLength, index);
        remaining -= keyLength + valueLength;
        src += keyLength + valueLength;
    }
}

/**
 * Fill in the body of a message to include any initial metadata, message
 * data, and trailing metadata from a stream op.
 * \param op
 *      Describes the data to include in the message.
 * \param msg
 *      Where to collect all of the message data; the payload field
 *      will be reset along with metadata corresponding to it.
 * \return
 *      The total number of bytes stored in msg->payload.
 */
size_t Wire::fillMessage(grpc_transport_stream_op_batch* op, Wire::Message *msg)
{
    // Number of bytes already stored at msg->payload.
    size_t offset = 0;

    if (op->send_initial_metadata) {
        size_t length = serializeMetadata(
                op->payload->send_initial_metadata.send_initial_metadata,
                msg->payload + offset);
        msg->hdr.initMdBytes = htonl(static_cast<uint32_t>(length));
        offset += length;
    }

    if (op->send_message) {
        uint32_t bytesLeft = op->payload->send_message.send_message->length();
        msg->hdr.messageBytes = htonl(bytesLeft);
        grpc_slice slice;
        while (bytesLeft != 0) {
            if (!op->payload->send_message.send_message->Next(bytesLeft,
                    nullptr)) {
                /* Should never reach here */
                GPR_ASSERT(false);
            }
            if (op->payload->send_message.send_message->Pull(&slice)
                    != GRPC_ERROR_NONE) {
                /* Should never reach here */
                GPR_ASSERT(false);
            }
            memcpy(msg->payload + offset, GRPC_SLICE_START_PTR(slice),
                    GRPC_SLICE_LENGTH(slice));
            offset += GRPC_SLICE_LENGTH(slice);
            bytesLeft -= GRPC_SLICE_LENGTH(slice);
            grpc_slice_unref(slice);
            gpr_log(GPR_INFO, "Copied %lu bytes into message buffer",
                    GRPC_SLICE_LENGTH(slice));
        }
    }
    
    if (op->send_trailing_metadata) {
        size_t length = serializeMetadata(
                op->payload->send_trailing_metadata.send_trailing_metadata,
                msg->payload + offset);
        msg->hdr.trailMdBytes = htonl(static_cast<uint32_t>(length));
        offset += length;
    }
    return offset;
}