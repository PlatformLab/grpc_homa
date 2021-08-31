#include "src/core/lib/transport/byte_stream.h"

#include "homa_listener.h"
#include "homa.h"
#include "util.h"

std::optional<HomaListener::Shared> HomaListener::shared;
gpr_once HomaListener::shared_once = GPR_ONCE_INIT;


/**
 * Constructor for HomaListeners.
 * \param port
 *      The Homa port number that this object will manage.
 */
HomaListener::HomaListener(grpc_server* server, int port)
    : transport()
    , vtable()
    , server(server->core_server.get())
    , activeRpcs()
    , mutex()
    , port(port)
    , fd(-1)
    , gfd(nullptr)
    , read_closure()
    , accept_stream_cb(nullptr)
    , accept_stream_data(nullptr)
{
    transport.vtable = &vtable;
    vtable.sizeof_stream =       sizeof(HomaStream);
    vtable.name =                "homa_server";
    vtable.init_stream =         init_stream;
    vtable.set_pollset =         set_pollset;
    vtable.set_pollset_set =     set_pollset_set;
    vtable.perform_stream_op =   perform_stream_op;
    vtable.perform_op =          perform_op;
    vtable.destroy_stream =      destroy_stream;
    vtable.destroy =             destroy;
    vtable.get_endpoint =        get_endpoint;
    GRPC_CLOSURE_INIT(&read_closure, onRead, this,
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
    Wire::init();
}

/**
 * The primary entry point to create a new HomaListener.
 * \param server
 *      gRPC server associated with this listener.
 * \param port
 *      The port number through which clients will make requests of
 *      this server.
 * 
 * \return
 *      Either a new HomaListener, or an existing one, if there was one for
 *      the given port. If an error occurs while initializing the port,
 *      a message is logged and nullptr is returned.
 */
HomaListener *HomaListener::Get(grpc_server* server, int port)
{
    HomaListener *lis;
    gpr_once_init(&shared_once, InitShared);
    {
        std::lock_guard<std::mutex> guard(shared->mutex);

        std::unordered_map<int, HomaListener *>::iterator it
                = shared->ports.find(port);
        if (it != shared->ports.end())
            return it->second;
        lis = new HomaListener(server, port);
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
    server->SetupTransport(&transport, NULL, server->channel_args(), nullptr);
}
/**
 * Callback invoked when a Homa socket becomes readable. Invokes
 * appropriate callbacks for the request.
 * \param arg
 *      Pointer to the HomaListener structure associated with the socket.
 * \param err
 *      Indicates whether the socket has an error condition.
 */
void HomaListener::onRead(void* arg, grpc_error* error)
{
    HomaListener *lis = static_cast<HomaListener*>(arg);
    StreamInit init;
    HomaIncoming::UniquePtr msg;
    
    if (error != GRPC_ERROR_NONE) {
        gpr_log(GPR_ERROR, "OnRead invoked with error: %s",
                grpc_error_string(error));
        return;
    }
    msg = HomaIncoming::read(lis->fd, HOMA_RECV_REQUEST|HOMA_RECV_NONBLOCKING);
    grpc_fd_notify_on_read(lis->gfd, &lis->read_closure);
    if (!msg) {
        return;
    }

    init.streamId = &msg->streamId;
    init.homaId = msg->homaId;
    init.stream = nullptr;
    
    if (lis->accept_stream_cb) {
        lis->accept_stream_cb(lis->accept_stream_data, &lis->transport, &init);
    }
    if (init.stream == nullptr) {
        gpr_log(GPR_INFO, "Stream doesn't appear to have been initialized.");
        return;
    }
    
    // Unpack the metadata and data and pass to gRPC.
    HomaStream *stream = init.stream;
    grpc_core::MutexLock lock(&stream->mutex);
    stream->handleIncoming(std::move(msg));
    gpr_log(GPR_INFO, "HomaListener::onRead done");
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

/**
 * Invoked by gRPC during a set_accept_stream callback to initialize a
 * new stream for an incoming RPC.
 * \param gt
 *      Pointer to the associated HomaListener.
 * \param gs
 *      Block of memory in which to initialize a stream.
 * \param refcount
 *      Externally owned reference to associate with the stream.
 * \param init_info
 *      Pointer to StreamInfo structure with info to initialize the Stream.
 * \param arena
 *      Use this for allocating storage for use by the stream (will
 *      be freed when the stream is destroyed).
 * 
 * \return
 *      Zero means success, nonzero means failure.
 */
int HomaListener::init_stream(grpc_transport* gt, grpc_stream* gs,
        grpc_stream_refcount* refcount, const void* init_info,
        grpc_core::Arena* arena)
{
    HomaListener *lis = containerOf(gt, &HomaListener::transport);
    HomaStream *stream = reinterpret_cast<HomaStream *>(gs);
    StreamInit *init = const_cast<StreamInit*>(
            reinterpret_cast<const StreamInit*>(init_info));
    gpr_log(GPR_INFO, "HomaListener::init_stream invoked");
    new (stream) HomaStream(*init->streamId, init->homaId, lis->fd, refcount,
            arena);
    init->stream = stream;
    return 0;
}

void HomaListener::set_pollset(grpc_transport* gt, grpc_stream* gs,
        grpc_pollset* pollset)
{
    gpr_log(GPR_INFO, "HomaListener::set_pollset invoked");
}

void HomaListener::set_pollset_set(grpc_transport* gt, grpc_stream* gs,
        grpc_pollset_set* pollset_set)
{
    gpr_log(GPR_INFO, "HomaListener::set_pollset_set invoked");
}

/**
 * This method is invoked by gRPC to perform one or more operations
 * on the stream associated with an RPC for which we are the server.
 * \param gt
 *      Points to the @transport field of a HomaListener.
 * \param gs
 *      Points to a HomaListener::Stream object.
 * \param op
 *      Describes the operation(s) to perform.
 */
void HomaListener::perform_stream_op(grpc_transport* gt, grpc_stream* gs,
        grpc_transport_stream_op_batch* op)
{
    HomaListener *lis = containerOf(gt, &HomaListener::transport);
    HomaStream *stream = reinterpret_cast<HomaStream*>(gs);
    grpc_core::MutexLock lock(&stream->mutex);
    grpc_error_handle error = GRPC_ERROR_NONE;
    
    gpr_log(GPR_INFO, "HomaListener::perform_stream_op invoked");
    if (op->cancel_stream) {
        gpr_log(GPR_INFO, "HomaListener::perform_stream_op: cancel "
                "stream ((%s)", grpc_error_std_string(
                op->payload->cancel_stream.cancel_error).c_str());
    }
    if (op->recv_initial_metadata || op->recv_message
            || op->recv_trailing_metadata) {
        stream->saveCallbacks(op);
        stream->transferData();
    }
    
    if (op->send_initial_metadata || op->send_message
            || op->send_trailing_metadata) {
        if (lis->fd < 0) {
            error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                    "No Homa socket open");
        } else {
            stream->xmit(op);
        }
    }
    
    if (op->on_complete) {
        grpc_core::ExecCtx::Run(DEBUG_LOCATION, op->on_complete,
                error);
    }
}

/**
 * Implements transport ops on the overall Homa listener.
 * \param gt
 *      Pointer to the Homa Listener.
 * \param op
 *      Information about the specific operation(s) to perform
 */
void HomaListener::perform_op(grpc_transport* gt, grpc_transport_op* op)
{
    HomaListener *lis = containerOf(gt, &HomaListener::transport);
    gpr_log(GPR_INFO, "HomaListener::perform_op invoked");
    if (op->start_connectivity_watch) {
        gpr_log(GPR_INFO, "HomaListener::perform_op invoked with "
                "start_connectivity_watch");
    }
    if (op->stop_connectivity_watch) {
        gpr_log(GPR_INFO, "HomaListener::perform_op invoked with "
                "stop_connectivity_watch");
    }
    if (op->disconnect_with_error != GRPC_ERROR_NONE) {
        gpr_log(GPR_INFO, "HomaListener::perform_op got "
                "disconnect_with_error: %s",
                grpc_error_string(op->disconnect_with_error));
        GRPC_ERROR_UNREF(op->disconnect_with_error);
    }
    if (op->goaway_error != GRPC_ERROR_NONE) {
        gpr_log(GPR_INFO, "HomaListener::perform_op got goaway_error: %s",
                grpc_error_string(op->goaway_error));
        GRPC_ERROR_UNREF(op->goaway_error);
    }
    if (op->set_accept_stream) {
        gpr_log(GPR_INFO, "HomaListener::perform_op invoked with "
                "set_accept_stream");
        lis->accept_stream_cb = op->set_accept_stream_fn;
        lis->accept_stream_data = op->set_accept_stream_user_data;
    }
    if (op->bind_pollset) {
        gpr_log(GPR_INFO, "HomaListener::perform_op invoked with "
                "bind_pollset");
    }
    if (op->bind_pollset_set) {
        gpr_log(GPR_INFO, "HomaListener::perform_op invoked with "
                "bind_pollset_set");
    }
    if (op->send_ping.on_initiate) {
        gpr_log(GPR_INFO, "HomaListener::perform_op invoked with "
                "send_ping.on_initiate");
    }
    if (op->send_ping.on_ack) {
        gpr_log(GPR_INFO, "HomaListener::perform_op invoked with "
                "send_ping.on_ack");
    }
    if (op->reset_connect_backoff) {
        gpr_log(GPR_INFO, "HomaListener::perform_op invoked with "
                "reset_connect_backoff");
    }

    grpc_core::ExecCtx::Run(DEBUG_LOCATION, op->on_consumed, GRPC_ERROR_NONE);
}

void HomaListener::destroy_stream(grpc_transport* gt, grpc_stream* gs,
        grpc_closure* closure)
{
    HomaStream *stream = reinterpret_cast<HomaStream*>(gs);
    gpr_log(GPR_INFO, "HomaListener::destroy_stream invoked");
    stream->~HomaStream();
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, closure, GRPC_ERROR_NONE);
}

void HomaListener::destroy(grpc_transport* gt)
{
    gpr_log(GPR_INFO, "HomaListener::destroy invoked");
}

grpc_endpoint* HomaListener::get_endpoint(grpc_transport* gt)
{
    gpr_log(GPR_INFO, "HomaListener::get_endpoint invoked");
    return nullptr;
}
