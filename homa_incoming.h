#ifndef HOMA_INCOMING_H
#define HOMA_INCOMING_H

#include <sys/uio.h>

#include <vector>

#include "homa.h"
#include "homa_socket.h"
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
            msg->sliceRefs.Unref({});
        }
    };

    typedef std::unique_ptr<HomaIncoming, UnrefIncoming> UniquePtr;

    explicit          HomaIncoming(HomaSocket *sock);
    explicit          HomaIncoming(HomaSocket *sock, int sequence, bool initMd,
                            size_t bodyLength, int firstValue,
                            bool messageComplete, bool trailMd);
                      ~HomaIncoming();
    size_t            addMetadata(size_t offset, ...);
    void              copyOut(void *dst, size_t offset, size_t length);
    void              deserializeMetadata(size_t offset, size_t length,
                            grpc_metadata_batch* batch);
    grpc_core::Slice  getSlice(size_t offset, size_t length);

    static UniquePtr  read(HomaSocket *sock, int flags, uint64_t *homaId,
                            grpc_error_handle *error);

    /**
     * Return a count of the number of contiguous bytes available at a
     * given offset in the message (or zero if the offset is outside
     * the range of the message).
     * \param offset
     *      Offset of a byte within the message.
     */
    size_t contiguous(size_t offset)
    {
        if (offset >= length) {
            return 0;
        }
        if ((offset >> HOMA_BPAGE_SHIFT) == (recvArgs.num_bpages-1)) {
            return length - offset;
        }
        return HOMA_BPAGE_SIZE - (offset & (HOMA_BPAGE_SIZE-1));
    }

    /**
     * Make a range of bytes from a message addressable in a contiguous
     * chunk.
     * \param offset
     *      Offset within the message of the first byte of the desired
     *      object.
     * \param auxSpace
     *      If the object is split across buffers in msg, it will
     *      be copied here to make it contiguous. If you know that the
     *      requested information will be contiguous in the message,
     *      this parameter can be specified as nullptr.
     * \tparam T
     *      Type of the desired object.
     * \return
     *      A pointer to contiguous memory containing the desired object,
     *      or nullptr if the desired object extends beyond the end of the
     *      message.
     */
    template <class T>
    T *get(size_t offset, T *auxSpace)
    {
        if ((offset + sizeof(T)) > length) {
            return nullptr;
        }
        size_t bufIndex = offset >> HOMA_BPAGE_SHIFT;
        size_t offsetInBuf = offset & (HOMA_BPAGE_SIZE-1);
        uint8_t *start = sock->getBufRegion() + recvArgs.bpage_offsets[bufIndex]
                + offsetInBuf;
        size_t cbytes = contiguous(offset);
        if (cbytes >= sizeof(T)) {
            return reinterpret_cast<T*>(start);
        }

        // Must copy the object to make it contiguous; it could span any
        // number of buffers.
        uint8_t *p = reinterpret_cast<uint8_t *>(auxSpace);
        memcpy(p, start, cbytes);
        for (size_t offsetInObj = cbytes; offsetInObj < sizeof(T);
                offsetInObj += cbytes) {
            bufIndex++;
            cbytes = ((sizeof(T) - offsetInObj) > HOMA_BPAGE_SIZE)
                    ? HOMA_BPAGE_SIZE : (sizeof(T) - offsetInObj);
            memcpy(p + offsetInObj,
                    sock->getBufRegion() + recvArgs.bpage_offsets[bufIndex],
                    cbytes);
        }
        return auxSpace;
    }

    /**
     * Returns the unique identifier for this message's stream.
     */
    StreamId& getStreamId()
    {
        return streamId;
    }


    // Keeps track of all outstanding references to this message
    // (such as a std::unique_ptr for the entire message, and metadata
    // keys and values).
    grpc_slice_refcount sliceRefs;

    // Information about stream (gRPC RPC) associated with the message.
    StreamId streamId;

    // Information about the Homa socket from which the message was read.
    HomaSocket *sock;

    // Passed as msg_control argument to recvmsg; contains information about
    // the incoming message (such as where its bytes are stored). Note that
    // buffers referenced here must eventually be returned to Homa.
    struct homa_recvmsg_args recvArgs;

    // Total length of the message.
    size_t length;

    // Sequence number for this message (extracted from hdr).
    int sequence;

    // Bytes of initial metadata in the message (extracted from hdr).
    uint32_t initMdLength;

    // Bytes of gRPC message payload. Set to 0 once message data has
    // been transferred to gRPC.
    uint32_t bodyLength;

    // Bytes of trailing metadata in the message (extracted from hdr).
    uint32_t trailMdLength;

    // If non-null, the target is incremented when this object is destroyed.
    int *destroyCounter;

    // If the value for a metadata item is longer than this, it will be
    // stored as a refcounted pointer into the message, rather than a
    // static slice. This is a variable so it can be modified for testing.
    size_t maxStaticMdLength;

    static void destroyer(grpc_slice_refcount *sliceRefs);

    Wire::Header *hdr()
    {
        return reinterpret_cast<Wire::Header*>(
                sock->getBufRegion() + recvArgs.bpage_offsets[0]);
    }
};

#endif // HOMA_INCOMING_H
