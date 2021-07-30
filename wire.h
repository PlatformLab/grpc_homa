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

/** Wire format of a Homa message. */
struct HomaMessage {
    // Unique identifier for this RPC (assigned by client).
    be32 id;
    
    // Number of bytes of initial metadata. Zero means initial metadata not
    // present in this message, -1 means initial metadata present but empty.
    be32 init_md_bytes;
    
    // Number of bytes of message data. Zero means no message present.
    be32 message_bytes;
    
    // Number of bytes of trailing metadata. Zero means trailing metadata not
    // present in this message, -1 means trailing metadata present but empty.
    be32 trailing_md_bytes;
    
    // Contains initial metadata followed by message data followed by
    // trailing metadata. Any of these may be absent.
    char payload[10000];
};

/** A scoping container for static methods. */
class Wire {
public:
    static size_t     appendMetadata(grpc_metadata_batch* batch, char *dest);
    static size_t     fillMessage(grpc_transport_stream_op_batch* op,
                        HomaMessage *msg);
    static void       logMetadata(char *buffer, size_t length);
};

#endif // WIRE_H