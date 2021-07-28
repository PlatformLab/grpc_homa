#ifndef WIRE_H
#define WIRE_H

/* This file defines the on-the-wire format of messages used to implement
 * gRPC over Homa.
 */

/* Values that are stored in network byte order ("big endian"). */
typedef int16_t be16;
typedef int32_t be32;
typedef int64_t be64;

/** Wire format of a Homa message. */
struct HomaMessage {
    /* Unique identifier for this RPC (assigned by client). */
    be32 id;
    
    /* Number of bytes used in payload. */
    be32 payloadLength;
    
    char payload[10000];
};

#endif // WIRE_H