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