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
 * Print to the log the contents of a block of metadata, as serialized
 * by appendMetadata.
 * \param buffer
 *      First byte of serialized data.
 * \param length
 *      Total amount of serialized data.
 * \param severity
 *      Log severity level to use for messages.
 */
void Wire::dumpMetadata(void *buffer, size_t length, gpr_log_severity severity)
{
    size_t remaining = length;
    uint8_t *src = static_cast<uint8_t *>(buffer);
    
    // Each iteration prints one metadata value
    while (remaining > 0) {
        Wire::Mdata* msgMd = reinterpret_cast<Wire::Mdata*>(src);
        if (remaining < sizeof(*msgMd)) {
             gpr_log(__FILE__, __LINE__, severity, "Not enough bytes for "
                    "metadata header: need %lu, have %lu",
                    sizeof(*msgMd), remaining);
             return;
        }
        int index = msgMd->index;
        uint32_t keyLength = ntohl(msgMd->keyLength);
        uint32_t valueLength = ntohl(msgMd->valueLength);
        remaining -= sizeof(*msgMd);
        src += sizeof(*msgMd);
        if (remaining < (keyLength + valueLength)) {
             gpr_log(__FILE__, __LINE__, severity, "Not enough bytes for "
                    "key and value: need %u, have %lu",
                    keyLength + valueLength, remaining);
             return;
        }
        gpr_log(__FILE__, __LINE__, severity,
                "Key: %.*s, value: %.*s, index: %d", keyLength, src,
                valueLength, src+keyLength, index);
        remaining -= keyLength + valueLength;
        src += keyLength + valueLength;
    }
}

/**
 * Log information about the contents of a Homa message header.
 * \param msg
 *      Address of the first byte of a Homa message (expected to contain
 *      a valid header).
 * \param severity
 *      Log severity level to use for messages.
 */
void Wire::dumpHeader(void *msg, gpr_log_severity severity)
{
    Wire::Header *hdr = static_cast<Wire::Header *>(msg);
    std::string s;
    char buffer[100];
    
    snprintf(buffer, sizeof(buffer), "id: %u, sequence %u",
            ntohl(hdr->streamId), ntohl(hdr->sequenceNum));
    s.append(buffer);
    if (hdr->initMdBytes) {
        snprintf(buffer, sizeof(buffer), ", initMdBytes %u",
                ntohl(hdr->initMdBytes));
        s.append(buffer);
    }
    if (hdr->messageBytes) {
        snprintf(buffer, sizeof(buffer), ", messageBytes %u",
                ntohl(hdr->messageBytes));
        s.append(buffer);
    }
    if (hdr->trailMdBytes) {
        snprintf(buffer, sizeof(buffer), ", trailMdBytes %u",
                ntohl(hdr->trailMdBytes));
        s.append(buffer);
    }
    if (hdr->flags & Header::initMdPresent) {
        s.append(", initMdPresent");
    }
    if (hdr->flags & Header::messageComplete) {
        s.append(", messageComplete");
    }
    if (hdr->flags & Header::trailMdPresent) {
        s.append(", trailMdPresent");
    }
    gpr_log(__FILE__, __LINE__, severity, "Header: %s", s.c_str());
}