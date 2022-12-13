#include <sys/mman.h>

#include "src/core/lib/transport/transport_impl.h"

#include "homa.h"

#include "homa_socket.h"

/**
 * Constructor for HomaSockets; opens the socket and sets up buffer space.
 * If an error occurs in setting up the socket then winter information will
 * be logged and getFd() will return -1.
 * \param domain
 *      Communication domain for the socket; must be AF_INET for IPv4 or
 *      AF_INET6 for IPv6.
 * \param port
 *      Homa port to bind to the socket. Must be either a Homa port number
 *      less than HOMA_MIN_DEFAULT_PORT or 0 (in which case Homa will assign
 *      an unused port number).
 */
HomaSocket::HomaSocket(int domain, int port)
    : fd(-1)
    , gfd(nullptr)
    , port(-1)
    , bufRegion(nullptr)
    , bufSize(0)
    , savedBuffers()
    , mutex()
{
    sockaddr_in_union addr{};
    socklen_t addr_size = sizeof(addr);
    int status;

    fd = socket(domain, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_HOMA);
    if (fd < 0) {
        gpr_log(GPR_ERROR, "Couldn't open Homa socket: %s\n", strerror(errno));
        goto error;
    }
    gfd = grpc_fd_create(fd, "homa-socket", true);

    // Bind to port, if needed, and retrieve port number.
    if (port > 0) {
        if (domain == AF_INET6) {
            addr.in6.sin6_family = AF_INET6;
            addr.in6.sin6_port = htons(port);
        } else {
            addr.in4.sin_family = AF_INET;
            addr.in4.sin_port = htons(port);
        }
        if (bind(fd, &addr.sa, sizeof(addr)) != 0) {
           gpr_log(GPR_ERROR, "Couldn't bind Homa socket to port %d: %s\n",
                   port, strerror(errno));
           goto error;
        }
    }
    getsockname(fd, &addr.sa, &addr_size);
    this->port =  ntohs((domain == AF_INET6) ? addr.in6.sin6_port
            : addr.in4.sin_port);

    // Set up the buffer region.
    bufSize = 1000*HOMA_BPAGE_SIZE;
    bufRegion = (uint8_t *) mmap(NULL, bufSize, PROT_READ|PROT_WRITE,
            MAP_PRIVATE|MAP_ANONYMOUS, 0, 0);
    if (bufRegion == MAP_FAILED) {
        gpr_log(GPR_ERROR,
                "Couldn't mmap buffer region for server on port %d: %s\n",
                port, strerror(errno));
        bufRegion = nullptr;
        bufSize = 0;
        goto error;
    }
    struct homa_set_buf_args setBufArgs;
    setBufArgs.start = bufRegion;
    setBufArgs.length = bufSize;
    status = setsockopt(fd, IPPROTO_HOMA, SO_HOMA_SET_BUF, &setBufArgs,
            sizeof(setBufArgs));
    if (status < 0) {
        gpr_log(GPR_ERROR,
                "Error in setsockopt(SO_HOMA_SET_BUF) for port %d: %s\n",
                port, strerror(errno));
        goto error;
    }
    return;

error:
    cleanup();
}

/**
 * HomaSocket constructor used during unit tests; doesn't actually open
 * a socket.
 * \param bufRegion
 *      Buffer region to use for the socket.
 */
HomaSocket::HomaSocket(uint8_t *bufRegion)
    : fd(-1)
    , gfd(nullptr)
    , port(-1)
    , bufRegion(bufRegion)
    , bufSize(0)
    , savedBuffers()
    , mutex()
{
}

/**
 * Destructor for HomaSockets. Closes the socket and releases buffer space.
 */
HomaSocket::~HomaSocket()
{
    cleanup();
}

/**
 * Release all resources associated with the socket, including closing the
 * socket itself.
 */
void HomaSocket::cleanup()
{
    if (bufRegion) {
        if (munmap(bufRegion, bufSize) != 0) {
            gpr_log(GPR_ERROR,
                    "Munmap failed for Homa socket with fd %d, port %d: %s\n",
                    fd, port, strerror(errno));
        }
        bufRegion = nullptr;
    }
    if (gfd) {
        // Note: grpc_fd_shutdown will close the fd.
        grpc_fd_shutdown(gfd, GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                "Homa socket destroyed"));
        grpc_fd_orphan(gfd, nullptr, nullptr, "goodbye");
        grpc_core::ExecCtx::Get()->Flush();
        gfd = nullptr;
    } else if (fd >= 0) {
        if (close(fd) < 0) {
            gpr_log(GPR_ERROR,
                    "close failed for Homa socket with fd %d, port %d: %s\n",
                    fd, port, strerror(errno));
        }
    }
    fd = -1;
}

/**
 * This method is called when the buffer space received in a previous
 * recvmsg call is no longer needed. It saves information about the
 * buffers, so that it can return that information in a later call to
 * getSavedBuffers().
 * \param recvArgs
 *      Structure in which Homa passed buffer information to the application.
 *      recvArgs->num_bpages will be set to 0 to indicate that all buffers
 *      have been claimed here.
 */
void HomaSocket::saveBuffers(struct homa_recvmsg_args *recvArgs)
{
    grpc_core::MutexLock lock(&mutex);
    for (uint32_t i = 0; i < recvArgs->num_bpages; i++) {
        savedBuffers.emplace_back(recvArgs->bpage_offsets[i]);
    }
    recvArgs->num_bpages = 0;
}

/**
 * This method is called before invoking Homa recvmsg; it adds as many
 * saved buffers as possible to recvArgs->buffers, so that they will be
 * returned to Homa as part of recvmsg.
 * \param recvArgs
 *      Struct that is about to be passed to Homa's recvmsg. May already
 *      contain some buffers to return.
 */
void HomaSocket::getSavedBuffers(struct homa_recvmsg_args *recvArgs)
{
    grpc_core::MutexLock lock(&mutex);
    uint32_t count = recvArgs->num_bpages;
    while ((count < HOMA_MAX_BPAGES) && !savedBuffers.empty()) {
        recvArgs->bpage_offsets[count] = savedBuffers.front();
        savedBuffers.pop_front();
        count++;
    }
    recvArgs->num_bpages = count;
}