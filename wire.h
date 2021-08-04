#ifndef WIRE_H
#define WIRE_H

#include "src/core/lib/transport/transport.h"

/* This file defines the on-the-wire format of messages used to implement
 * gRPC over Homa.
 */

/* Values that are stored in network byte order ("big endian"). */
typedef int16_t be16;
typedef int32_t be32;
typedef int64_t be64;

/**
 * This class defines the on-the-wire format of messages used to implement
 * gRPC over Homa, and also provides methods for serializing and
 * deserializing messages.
 */
class Wire {
public:
    /** Every request a response message starts with this information. */
    struct Header {
        // Unique identifier for this RPC (assigned by client).
        be32 id;

        // Number of bytes of initial metadata (may be zero).
        be32 initMdBytes;

        // Number of bytes of message data (may be zero).
        be32 messageBytes;

        // Number of bytes of trailing metadata (may be zero).
        be32 trailMdBytes;
    } __attribute__((packed));
    
    /** Each metadata value has the following format. */
    struct Mdata {
        // If this element is a "callout" (one that can be accessed by
        // index), this is the index value. GRPC_BATCH_CALLOUTS_COUNT
        // means not a callout.
        uint8_t index;
        
        // Number of bytes in the key for this item.
        be32 keyLength;
        
        // Number of bytes in the value for this item.
        be32 valueLength;
        
        // The key is stored starting here, followed by the value.
        char data[0];
    } __attribute__((packed));
    
    /** Temporary structure describing an entire message (fixed size). */
    struct Message {
        Header hdr;
        
        // Contains initial metadata followed by message data followed by
        // trailing metadata. Any of these may be absent.
        uint8_t payload[10000];
    } __attribute__((packed));
    
    // An array of special reference counts used for callout metadata
    // elements; element i contains a hidden value (same as i) that identifies
    // the location of its metadata value in grpc_metadata_batch_callouts.
    static grpc_core::StaticSliceRefcount *calloutRefs[GRPC_BATCH_CALLOUTS_COUNT];
    
    static void       deserializeMetadata(uint8_t *src, size_t length,
                            grpc_metadata_batch* batch,
                            grpc_core::Arena* arena);
    static void       dumpMetadata(uint8_t *buffer, size_t length);
    static size_t     fillMessage(grpc_transport_stream_op_batch* op,
                            Message *msg);
    static void       init();
    static size_t     serializeMetadata(grpc_metadata_batch* batch,
                            uint8_t *dest);
};

#endif // WIRE_H