#ifndef HOMA_SOCKET_H
#define HOMA_SOCKET_H

#include <deque>

#include "src/core/lib/iomgr/ev_posix.h"

#include "homa.h"

/**
 * An instance of this class doors state associated with an open Homa
 * socket. It particular, it manages the  buffer region used for
 * incoming messages.
 */
class HomaSocket {
public:
    HomaSocket(int domain, int port);
    HomaSocket(uint8_t *bufRegion);
    ~HomaSocket(void);
    void      saveBuffers(struct homa_recvmsg_args *recvArgs);
    void      getSavedBuffers(struct homa_recvmsg_args *recvArgs);

    //Returns the file descriptor associated with this object, or -1
    // if the constructor failed to initialize the socket.
    inline int getFd() const
    {
        return fd;
    }

    /**
     * Returns GRPCs handle for the file descriptor for this socket, or
     * nullptr if the socket was not successfully opened.
     */
    inline grpc_fd *getGfd() const
    {
        return gfd;
    }

    /**
     * Returns the port number associated with this socket; the value
     * is undefined if the constructor failed to initialize the socket.
     */
    inline int getPort() const
    {
        return port;
    }

    /**
     * Returns the base address of the receive buffer region for this
     * socket. If the instructor failed to initialize the socket then
     * nullptr is returned.
     * @return
     */
    inline uint8_t *getBufRegion() const
    {
        return bufRegion;
    }

    // The info below should be treated as private; it is left public
    // in order to enable unit testing.

    // File descriptor for a Homa socket, or -1 if the constructor
    // failed to set up the socket properly.
    int fd;

    // GRPC token corresponding to fd, or nullptr if the constructor failed.
    grpc_fd *gfd;

    // Homa port number assigned to this socket.
    int port;

    // First byte of memory region for buffer space for incoming messages.
    // or nullptr if buffer space has not been allocated. This is an mmapped
    // region.
    uint8_t *bufRegion;

    // Size of the buffer region at *bufRegion, in bytes.
    size_t bufSize;

    // Tokens for buffers that need to be returned eventually to Homa.
    // Each token is an entry from the buffers array in a
    // struct homa_recvmsg_control object.
    std::deque<uint32_t> savedBuffers;

    // Must be held whenever accessing savedBuffers.
    grpc_core::Mutex mutex;

    void cleanup();
};

#endif // HOMA_SOCK