#ifndef HOMA_INCOMING_H
#define HOMA_INCOMING_H

#include <sys/uio.h>

#include <vector>

#include "stream_id.h"
#include "wire.h"

/**
 * An instance of this class describes one incoming Homa message
 * (either a request or a response).
 */
class HomaIncoming {
public:
    // This class is used as the deleter for Incoming::UniquePtr,
    // so that sliceRefs can count a std::unique_ptr along with other
    // refs to the Incoming.
    struct UnrefIncoming {
        void operator()(HomaIncoming* msg)
        {
            msg->sliceRefs.Unref();
        }
    };

    typedef std::unique_ptr<HomaIncoming, UnrefIncoming> UniquePtr;

    explicit          HomaIncoming();
    explicit          HomaIncoming(int sequence, bool initMd,
                            size_t messageLength, size_t tailLength,
                            int firstValue, bool messageComplete, bool trailMd);
                      ~HomaIncoming();
    size_t            addMetadata(size_t offset, size_t staticLength, ...);
    void              copyOut(void *dst, size_t offset, size_t length);
    void              deserializeMetadata(size_t offset, size_t length,
                            grpc_metadata_batch* batch,
                            grpc_core::Arena* arena);
    grpc_slice        getSlice(size_t offset, size_t length);
    grpc_slice        getStaticSlice(size_t offset, size_t length,
                            grpc_core::Arena *arena);

    static UniquePtr  read(int fd, int flags, uint64_t *homaId,
                            grpc_error_handle *error);

    /**
     * Make a range of bytes from a message addressable in a contiguous
     * chunk.
     * \param offset
     *      Offset within the message of the first byte of the desired
     *      object.
     * \param buffer
     *      If the object is split across buffers in msg, it will
     *      be copied here to make it contiguous.
     * \tparam T
     *      Type of the desired object.
     * \return
     *      A pointer to contiguous memory containing the desired object.
     */
    template <class T>
    T *getBytes(size_t offset, T *buffer)
    {
        // See if the object is already contiguous in the first part of the
        // message.
        if ((offset + sizeof(T)) <= baseLength) {
            return reinterpret_cast<T *>(initialPayload + offset);
        }

        // See if the offset is already contiguous in the tail.
        if (offset >= baseLength) {
            return reinterpret_cast<T *>(
                    tail.data() + (offset - baseLength));
        }

        // Must copy the object to make it contiguous.
        uint8_t *p = reinterpret_cast<uint8_t *>(buffer);
        size_t baseBytes = baseLength - offset;
        memcpy(p, initialPayload + offset, baseBytes);
        memcpy(p + baseBytes, tail.data(), sizeof(T) - baseBytes);
        return buffer;
    }

    /**
     * Returns the unique identifier for this message's stream.
     */
    StreamId& getStreamId()
    {
        return streamId;
    }

    // Used by sliceRefs. Don't access directly.
    std::atomic<size_t> refs{1};

    // Keeps track of all outstanding references to this message
    // (such as a std::unique_ptr for the entire message, and metadata
    // keys and values).
    grpc_slice_refcount sliceRefs;

    // Information about stream (gRPC RPC) associated with the message.
    StreamId streamId;

    // Total length of the message (may be longer than the space
    // available in hdr and initialPayload).
    size_t length;

    // Number of bytes actually stored in hdr and initialPayload
    size_t baseLength;

    // Sequence number for this message (extracted from hdr).
    int sequence;

    // Bytes of initial metadata in the message (extracted from hdr).
    uint32_t initMdLength;

    // Bytes of gRPC message initialPayload in the message (extracted from hdr).
    // Set to 0 once message data has been transferred to gRPC.
    uint32_t messageLength;

    // Bytes of trailing metadata in the message (extracted from hdr).
    uint32_t trailMdLength;

    // The first part of the message, enough to hold short messages.
    uint8_t initialPayload[10000];

    // If the entire message doesn't fit in hdr and initialPayload, the
    // remainder will be read here.
    std::vector<uint8_t> tail;

    // If non-null, the target is incremented when this object is destroyed.
    int *destroyCounter;

    // If the value for a metadata item is longer than this, it will be
    // stored as a refcounted pointer into the message, rather than a
    // static slice. This is a variable so it can be modified for testing.
    size_t maxStaticMdLength;

    static void destroyer(void* arg);

    Wire::Header *hdr()
    {
        return reinterpret_cast<Wire::Header*>(initialPayload);
    }
};

#endif // HOMA_INCOMING_H
