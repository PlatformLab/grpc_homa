#include <arpa/inet.h>
#include <string.h>

#include <string>

#include "wire.h"

/**
 * Serialize a batch of metadata and append it to a message buffer.
 * \param batch
 *      All of the key-value pairs here will be appended.
 * \param dest
 *      Where to store the serialized data (presumably a message buffer?).
 * \return
 *      The total number of bytes appended at dest.
 */
size_t Wire::appendMetadata(grpc_metadata_batch* batch, char *dest)
{
    // For each metadata item, two string values are appended to dest (first
    // key, then value). Each value consists of a 4-byte length (in network
    // byte order) followed by that many bytes of key or value data.
    size_t length = 0;
    for (grpc_linked_mdelem* md = batch->list.head; md != nullptr;
            md = md->next) {
        const grpc_slice key = GRPC_MDKEY(md->md);
        const grpc_slice value = GRPC_MDVALUE(md->md);
        uint32_t sliceLength = GRPC_SLICE_LENGTH(key);
        *reinterpret_cast<int32_t*>(dest) = htonl(sliceLength);
        memcpy(dest+4, GRPC_SLICE_START_PTR(key), sliceLength);
        length += sliceLength + 4;
        dest += sliceLength + 4;
        
        sliceLength = GRPC_SLICE_LENGTH(value);
        *reinterpret_cast<int32_t*>(dest) = htonl(sliceLength);
        memcpy(dest+4, GRPC_SLICE_START_PTR(value), sliceLength);
        length += sliceLength + 4;
        dest += sliceLength + 4;
    }
    
    return length;
}

/**
 * Print to the log the contents of a block of metadata, as serialized
 * by appendMetadata.
 * \param buffer
 *      First byte of serialized data.
 * \param length
 *      Total amount of serialized data.
 */
void Wire::logMetadata(char *buffer, size_t length)
{
    size_t remaining = length;
    char *src = buffer;
    std::string key;
    while (remaining > 0) {
        if (remaining < 4) {
             gpr_log(GPR_INFO, "Not enough bytes for %s length: only "
                    "%lu bytes remaining",
                    key.empty() ? "key" : "value", remaining);
             return;
        }
        uint32_t length = ntohl(* reinterpret_cast<int32_t*>(src));
        remaining -= 4;
        src += 4;
        if (remaining < length) {
             gpr_log(GPR_INFO, "Not enough bytes for %s data: %lu bytes "
                    "remaining, but need %u bytes",
                    key.empty() ? "key" : "value", remaining, length);
             return;
        }
        if (key.empty()) {
            key.append(src, length);
        } else {
            gpr_log(GPR_INFO, "Key: %s, value: %*s", key.c_str(), length, src);
            key.erase();
        }
        remaining -= length;
        src += length;
    }
    if (!key.empty()) {
        gpr_log(GPR_INFO, "No value for '%s'", key.c_str());
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
size_t Wire::fillMessage(grpc_transport_stream_op_batch* op, HomaMessage *msg)
{
    // Number of bytes already stored at msg->payload.
    size_t offset = 0;

    if (op->send_initial_metadata) {
        size_t length = appendMetadata(
                op->payload->send_initial_metadata.send_initial_metadata,
                msg->payload + offset);
        if (length != 0) {
            msg->init_md_bytes = htonl(static_cast<uint32_t>(length));
        } else {
            msg->init_md_bytes = ~0;
        }
        offset += length;
    }

    if (op->send_message) {
        uint32_t length = op->payload->send_message.send_message->length();
        msg->message_bytes = htonl(static_cast<uint32_t>(length));
        grpc_slice slice;
        while (offset < length) {
            if (!op->payload->send_message.send_message->Next(length,
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
            grpc_slice_unref(slice);
            gpr_log(GPR_INFO, "Copied %lu bytes into message buffer",
                    GRPC_SLICE_LENGTH(slice));
        }
    }
    
    if (op->send_trailing_metadata) {
        size_t length = appendMetadata(
                op->payload->send_trailing_metadata.send_trailing_metadata,
                msg->payload + offset);
        if (length != 0) {
            msg->trailing_md_bytes = htonl(static_cast<uint32_t>(length));
        } else {
            msg->trailing_md_bytes = ~0;
        }
        offset += length;
    }
    return offset;
}