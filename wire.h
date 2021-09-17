#ifndef WIRE_H
#define WIRE_H

#include "src/core/lib/transport/transport.h"

/* This file defines the on-the-wire format of messages used to implement
 * gRPC over Homa.
 */

// Values that are stored in network byte order ("big endian").
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
    /** 
     * Every Homa RPC (whether request or response) starts with this
     * information.
     */
    struct Header {
        // Unique identifier for this stream (all messages for this RPC
        // will use the same identifier).
        be32 streamId;
        
        // Position of this Homa messages among all of those sent on
        // this stream. Used on the other end to make sure that messages
        // are processed in order. The first number for each stream is 1.
        be32 sequenceNum;

        // Number of bytes of initial metadata (may be zero), which
        // follows this header in the Homa RPC.
        be32 initMdBytes;

        // Number of bytes of trailing metadata (may be zero), which
        // follows the initial metadata.
        be32 trailMdBytes;

        // Number of bytes of gRPC message data (may be zero), which follows
        // the trailing metadata.
        be32 messageBytes;
        
        // ORed combination of one or more flag bits defined below.
        uint8_t flags;
        
        // Flag bit indicating that this message contains all available
        // initial metadata (possibly none).
        static const int initMdPresent = 1;
        
        // Flag bit indicating that, as of this Homa RPC, all message
        // data has been sent. If the message data is too long to fit
        // in a single message, only the last message has this bit set.
        static const int messageComplete = 2;
        
        // Flag bit indicating that this message contains all available
        // trailing metadata (possibly none).
        static const int trailMdPresent = 4;
        
        // Flag bit indicating that this is a request message used
        // for additional streaming data (it's not the first request
        // for an RPC). Streaming requests can be sent by either the
        // client or the server.
        static const int streamRequest = 8;
        
        // Flag bit indicating that this is a response for a streaming
        // request.
        static const int streamResponse = 16;
        
        Header(int streamId, int sequence, int initMdBytes, int trailMdBytes,
                int messageBytes)
            : streamId(htonl(streamId))
            , sequenceNum(htonl(sequence))
            , initMdBytes(htonl(initMdBytes))
            , trailMdBytes(htonl(trailMdBytes))
            , messageBytes(htonl(messageBytes))
            , flags(0)
        { }
        
        Header(int streamId, int sequence)
            : streamId(htonl(streamId))
            , sequenceNum(htonl(sequence))
            , initMdBytes(0)
            , trailMdBytes(0)
            , messageBytes(0)
            , flags(0)
        { }
        
        Header()
            : streamId()
            , sequenceNum()
            , initMdBytes()
            , trailMdBytes()
            , messageBytes()
            , flags()
        { }
                
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
    
    // An array of special reference counts used for callout metadata
    // elements; element i contains a hidden value (same as i) that identifies
    // the location of its metadata value in grpc_metadata_batch_callouts.
    static grpc_core::StaticSliceRefcount *calloutRefs[GRPC_BATCH_CALLOUTS_COUNT];

    static void       dumpHeader(void *msg, gpr_log_severity severity);
    static void       dumpMetadata(void *buffer, size_t length,
                            gpr_log_severity severity = GPR_LOG_SEVERITY_INFO);
    static void       init();
};

#endif // WIRE_H