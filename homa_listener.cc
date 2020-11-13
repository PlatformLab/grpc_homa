#include "homa_listener.h"
#include "homa.h"

std::optional<HomaListener::Shared> HomaListener::shared;
gpr_once HomaListener::shared_once = GPR_ONCE_INIT;

/**
 * Constructor for HomaListeners.
 * \param port
 *      The Homa port number that this object will manage.
 */
HomaListener::HomaListener(int port)
    : port(port)
    , fd(-1)
    , gfd(nullptr)
    , read_closure()
{
    GRPC_CLOSURE_INIT(&read_closure, OnRead, this,
            grpc_schedule_on_exec_ctx);
}

HomaListener::~HomaListener()
{
    std::lock_guard<std::mutex> guard(shared->mutex);
    shared->ports.erase(port);
    grpc_fd_shutdown(gfd,
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("Homa listener destroyed"));
}

/**
 * Invoked through the gpr_once mechanism to initialize the ports_mu
 * mutex.
 */
void HomaListener::InitShared(void)
{
    shared.emplace();
}

/**
 * The primary entry point to create a new HomaListener.
 * \param port
 *      The port number through which clients will make requests of
 *      this server.
 * 
 * \return
 *      Either a new HomaListener, or an existing one, if there was one for
 *      the given port. If an error occurs while initializing the port,
 *      a message is logged and nullptr is returned.
 */
HomaListener *HomaListener::Get(int port)
{
    HomaListener *lis;
    gpr_once_init(&shared_once, InitShared);
    {
        std::lock_guard<std::mutex> guard(shared->mutex);

        std::unordered_map<int, HomaListener *>::iterator it
                = shared->ports.find(port);
        if (it != shared->ports.end())
            return it->second;
        lis = new HomaListener(port);
        shared->ports[port] = lis;
    }
    
    lis->fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_HOMA);
	if (lis->fd < 0) {
        gpr_log(GPR_ERROR, "Couldn't open Homa socket: %s\n", strerror(errno));
		return nullptr;
	}
    struct sockaddr_in addr_in;
    memset(&addr_in, 0, sizeof(addr_in));
    addr_in.sin_family = AF_INET;
    addr_in.sin_port = htons(port);
    if (bind(lis->fd, (struct sockaddr *) &addr_in,
            sizeof(addr_in)) != 0) {
        gpr_log(GPR_ERROR, "Couldn't bind Homa socket to port %d: %s\n", port,
                strerror(errno));
        return nullptr;
    }
    
    char name[30];
    snprintf(name, sizeof(name), "homa-socket:%d", port);
    lis->gfd = grpc_fd_create(lis->fd, name, true);
    
    return lis;
}

void HomaListener::SetOnDestroyDone(grpc_closure* on_destroy_done)
{
}

/**
 * Invoked to start a listener running (unclear why things are done
 * here rather than when the listener is created).
 */
void HomaListener::Start(grpc_core::Server* server,
            const std::vector<grpc_pollset*>* pollsets)
{
    for (size_t i = 0; i < pollsets->size(); i++) {
        grpc_pollset_add_fd((*pollsets)[i], gfd);
    }
    grpc_fd_notify_on_read(gfd, &read_closure);
}

/**
 * Callback invoked when a Homa socket becomes readable. Invokes
 * appropriate callbacks for the request.
 * \param arg
 *      Pointer to the HomaListener structure associated with the socket.
 * \param err
 *      Indicates whether the socket has an error condition.
 */
void HomaListener::OnRead(void* arg, grpc_error* err)
{
//    HomaListener *lis = static_cast<HomaListener*>(arg);
    gpr_log(GPR_ERROR, "Got Homa request but don't know how to handle it");
}

grpc_core::channelz::ListenSocketNode*
        HomaListener::channelz_listen_socket_node() const
{
    return nullptr;
}

/**
 * Invoked to handle deletion of this object. No special actions needed here.
 */
void HomaListener::Orphan()
{
    delete this;
}