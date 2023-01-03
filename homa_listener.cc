#include "src/core/lib/transport/byte_stream.h"

#include "homa_listener.h"
#include "homa.h"
#include "time_trace.h"
#include "util.h"

std::optional<HomaListener::Shared> HomaListener::shared;
gpr_once HomaListener::shared_once = GPR_ONCE_INIT;

/**
 * Adds a Homa listening port to a server.
 * \param addr
 *      Has the syntax "homa:<port>"; indicates the port on which to listen.
 *      Also accepts "addr:port" for IPv4, but the IP address is ignored.
 *      Also accepts "[addr]:port" for IPv6, but the IP address is ignored.
 * \param server
 *      Add the listening port to this server.
 * \return
 *      Returns the port number on success, zero on failure.
 */
int HomaListener::InsecureCredentials::AddPortToServer(const std::string& addr,
        grpc_server* server)
{
    int port;
    char* end;
    bool ipv6 = false;

    const char* cursor = addr.c_str();
    if (*cursor == '[') {
        ipv6 = true;
        cursor = strchr(cursor, ']');
    }
    if (cursor != nullptr) {
        cursor = strchr(cursor, ':');
    }
    if (cursor == nullptr) {
        gpr_log(GPR_ERROR, "bad Homa listener spec '%s', must be 'homa|IP:port'",
                addr.c_str());
        return 0;
    };
    port = strtol(cursor+1, &end, 10);
    if ((*end != '\0') || (port < 0)) {
        gpr_log(GPR_ERROR,
                "bad Homa listener spec '%s', must be between 0 and %d, "
                "in decimal", addr.c_str(), HOMA_MIN_DEFAULT_PORT-1);
        return 0;
    }
    if (end == cursor+1) {
        gpr_log(GPR_ERROR, "bad Homa listener spec '%s' (no port number)",
                addr.c_str());
        return 0;
    }
    HomaListener *listener = HomaListener::Get(server, &port, ipv6);
    if (listener) {
        grpc_core::Server* core_server = grpc_core::Server::FromC(server);
        core_server->AddListener(grpc_core::OrphanablePtr
                <grpc_core::Server::ListenerInterface>(listener));
    }
    return port;
}

/**
 * Modifies a ServerBuilder so that Homa will listen on a given port.
 * \param addr
 *      Has the syntax "homa:<port>"; indicates the port on which to listen.
 * \param builder
 *      Add the listener to this ServerBuilder.
 * \return
 */
std::shared_ptr<grpc::ServerCredentials> HomaListener::insecureCredentials(void)
{
    return std::shared_ptr<grpc::ServerCredentials>(new InsecureCredentials());
}

/**
 * Invoked through the gpr_once mechanism to initialize shared info.
 */
void HomaListener::InitShared(void)
{
    shared.emplace();
    Wire::init();
    shared->vtable.sizeof_stream =       sizeof(HomaStream);
    shared->vtable.name =                "homa_server";
    shared->vtable.init_stream =         Transport::init_stream;
    shared->vtable.set_pollset =         Transport::set_pollset;
    shared->vtable.set_pollset_set =     Transport::set_pollset_set;
    shared->vtable.perform_stream_op =   Transport::perform_stream_op;
    shared->vtable.perform_op =          Transport::perform_op;
    shared->vtable.destroy_stream =      Transport::destroy_stream;
    shared->vtable.destroy =             Transport::destroy;
    shared->vtable.get_endpoint =        Transport::get_endpoint;
}

/**
 * Constructor for HomaListeners.
 * \param server
 *      Server that this listener will be associated with (nullptr for
 *      testing).
 * \param port
 *      The Homa port number that this object will manage. If zero, then
 *      the port number is chosen by Homa; the chosen value will be
 *      returned here. Zero is returned if the socket couldn't be opened.
 */
HomaListener::HomaListener(grpc_server* server, int *port, bool ipv6)
    : transport(new Transport(server, port, ipv6))
    , port(*port)
    , on_destroy_done(nullptr)
{
}

HomaListener::~HomaListener()
{
    if (port) {
        grpc_core::MutexLock lock(&shared->mutex);
        shared->ports.erase(port);
    }
    transport->shutdown();
    auto ctx = grpc_core::ExecCtx::Get();
    if (ctx) {
      ctx->Flush();
    } else {
      delete transport;
    }
    if (on_destroy_done) {
        grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_destroy_done, GRPC_ERROR_NONE);
        grpc_core::ExecCtx::Get()->Flush();
    }
}

/**
 * The primary entry point to create a new HomaListener.
 * \param server
 *      gRPC server associated with this listener.
 * \param port
 *      The port number through which clients will make requests of
 *      this server. Zero means that Homa will allocate a port number;
 *      the actual value will be returned here.
 *
 * \return
 *      Either a new HomaListener, or an existing one, if there was one for
 *      the given port. If an error occurs while initializing the port,
 *      a message is logged and nullptr is returned.
 */
HomaListener *HomaListener::Get(grpc_server* server, int *port, bool ipv6)
{
    grpc_core::ExecCtx exec_ctx;
    HomaListener *lis = nullptr;
    gpr_once_init(&shared_once, InitShared);
    if (*port) {
        grpc_core::MutexLock lock(&shared->mutex);

        std::unordered_map<int, HomaListener *>::iterator it
                = shared->ports.find(*port);
        if (it != shared->ports.end())
            return it->second;
    }
    lis = new HomaListener(server, port, ipv6);
    if (*port == 0) {
        delete lis;
        return nullptr;
    }
    {
        grpc_core::MutexLock lock(&shared->mutex);

        std::unordered_map<int, HomaListener *>::iterator it
                = shared->ports.find(*port);
        if (it != shared->ports.end()) {
            delete lis;
            return it->second;
        }
        shared->ports[*port] = lis;
    }
    return lis;
}

void HomaListener::SetOnDestroyDone(grpc_closure* on_destroy_done_in)
{
    on_destroy_done = on_destroy_done_in;
}

/**
 * Invoked to start a listener running (unclear why things are done
 * here rather than when the listener is created).
 *      gRPD server associated with this Transport.
 * \param pollset
 *      Event handlers need to get added to all of these.
 */
void HomaListener::Start(grpc_core::Server* server,
            const std::vector<grpc_pollset*>* pollsets)
{
    transport->start(server, pollsets);
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
 *  Constructor for Transports.
 * \param server
 *      gRPC server associated with this listener. nullptr means we're
 *      running unit tests.
 * \param port
 *      The port number through which clients will make requests of
 *      this server. Zero means that Homa will allocate a port number;
 *      the actual value will be returned here.  If 0 is returned, it
 *      means an error occurred.
 */
HomaListener::Transport::Transport(grpc_server* server, int *port, bool ipv6)
    : vtable()
    , server(nullptr)
    , activeRpcs()
    , mutex()
    , sock(ipv6 ? AF_INET6 : AF_INET, *port)
    , read_closure()
    , state_tracker("homa_transport", GRPC_CHANNEL_READY)
    , accept_stream_cb(nullptr)
    , accept_stream_data(nullptr)
{
    vtable.vtable = &shared->vtable;
    GRPC_CLOSURE_INIT(&read_closure, onRead, this, grpc_schedule_on_exec_ctx);
    if (server) {
        this->server = grpc_core::Server::FromC(server);
    }
    // In case the caller asked for port zero this updates *port to reflect
    // the port that the kernel allocated:
    *port = sock.getPort();
}

/**
 * Destructor for Transports.
 */
HomaListener::Transport::~Transport()
{
    // Nothing to do here: shutdown did it all.
}

/**
 * Implements the listener's start functionality.
 * \param server
 *      gRPD server associated with this Transport.
 * \param pollset
 *      Event handlers need to get added to all of these.
 */
void HomaListener::Transport::start(grpc_core::Server* server,
            const std::vector<grpc_pollset*>* pollsets)
{
    char name[30];

    snprintf(name, sizeof(name), "homa-socket:%d", sock.getPort());
    for (size_t i = 0; i < pollsets->size(); i++) {
        grpc_pollset_add_fd((*pollsets)[i], sock.getGfd());
    }
    grpc_fd_notify_on_read(sock.getGfd(), &read_closure);
    server->SetupTransport(&vtable, NULL, server->channel_args(),
            nullptr);
}

/**
 * Invoked when the Listener object is destroyed.
 */
void HomaListener::Transport::shutdown()
{
    state_tracker.SetState(GRPC_CHANNEL_SHUTDOWN, absl::OkStatus(),
            "error");
    ::shutdown(sock.getFd(), SHUT_RDWR);
}

/**
 * Find the stream corresponding to an incoming message, or create a new
 * stream if there is no existing one.
 * \param msg
 *      Incoming message for which a HomaStream is needed
 * \param lock
 *      This object will be constructed to lock the stream.
 * \return
 *      The stream that corresponds to @msg. The stream will be locked.
 *      If there was an error creating the stream, then nullptr is returned
 *      and streamLock isn't locked.
 */
HomaStream *HomaListener::Transport::getStream(HomaIncoming *msg,
        std::optional<grpc_core::MutexLock>& streamLock)
{
    HomaStream *stream;
    grpc_core::MutexLock lock(&mutex);

    ActiveIterator it = activeRpcs.find(msg->streamId);
    if (it != activeRpcs.end()) {
        stream = it->second;
        goto done;
    }

    // Must create a new HomaStream.
    StreamInit init;
    init.streamId = &msg->streamId;
    init.stream = nullptr;
    if (accept_stream_cb) {
        tt("Calling accept_stream_cb");
        accept_stream_cb(accept_stream_data, &vtable, &init);
        tt("accept_stream_cb returned");
    }
    if (init.stream == nullptr) {
        gpr_log(GPR_INFO, "Stream doesn't appear to have been initialized.");
        return nullptr;
    }
    stream = init.stream;
    activeRpcs[msg->streamId] = stream;

done:
    streamLock.emplace(&stream->mutex);
    return stream;
}

/**
 * Callback invoked when a Homa socket becomes readable. Invokes
 * appropriate callbacks for the request.
 * \param arg
 *      Pointer to the HomaListener structure associated with the socket.
 * \param err
 *      Indicates whether the socket has an error condition.
 */
void HomaListener::Transport::onRead(void* arg, grpc_error* error)
{
    Transport *trans = static_cast<Transport*>(arg);
    uint64_t homaId;

    if (error != GRPC_ERROR_NONE) {
        gpr_log(GPR_DEBUG, "OnRead invoked with error: %s",
                grpc_error_string(error));
        return;
    }
    while (true) {
        std::optional<grpc_core::MutexLock> streamLock;
        grpc_error_handle error;
        HomaIncoming::UniquePtr msg = HomaIncoming::read(&trans->sock,
                HOMA_RECVMSG_REQUEST|HOMA_RECVMSG_RESPONSE|HOMA_RECVMSG_NONBLOCKING,
                &homaId, &error);
        if ((error != GRPC_ERROR_NONE) || !msg) {
            break;
        }

        HomaStream *stream = trans->getStream(msg.get(), streamLock);
        stream->handleIncoming(std::move(msg), homaId);
    }
    grpc_fd_notify_on_read(trans->sock.getGfd(), &trans->read_closure);
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
int HomaListener::Transport::init_stream(grpc_transport* gt, grpc_stream* gs,
        grpc_stream_refcount* refcount, const void* init_info,
        grpc_core::Arena* arena)
{
    Transport *trans = containerOf(gt, &Transport::vtable);
    HomaStream *stream = reinterpret_cast<HomaStream *>(gs);
    StreamInit *init = const_cast<StreamInit*>(
            reinterpret_cast<const StreamInit*>(init_info));
    new (stream) HomaStream(*init->streamId, trans->sock.getFd(), refcount,
            arena);
    init->stream = stream;
    return 0;
}

void HomaListener::Transport::set_pollset(grpc_transport* gt, grpc_stream* gs,
        grpc_pollset* pollset)
{
    gpr_log(GPR_INFO, "HomaListener::Transport::set_pollset invoked");
}

void HomaListener::Transport::set_pollset_set(grpc_transport* gt, grpc_stream* gs,
        grpc_pollset_set* pollset_set)
{
    gpr_log(GPR_INFO, "HomaListener::Transport::set_pollset_set invoked");
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
void HomaListener::Transport::perform_stream_op(grpc_transport* gt, grpc_stream* gs,
        grpc_transport_stream_op_batch* op)
{
    Transport *trans = containerOf(gt, &Transport::vtable);
    HomaStream *stream = reinterpret_cast<HomaStream*>(gs);
    grpc_core::MutexLock lock(&stream->mutex);
    grpc_error_handle error = GRPC_ERROR_NONE;

    if (op->cancel_stream) {
        stream->cancelPeer();
        stream->notifyError(op->payload->cancel_stream.cancel_error);
    }
    if (op->recv_initial_metadata || op->recv_message
            || op->recv_trailing_metadata) {
        stream->saveCallbacks(op);
        stream->transferData();
    }

    if (op->send_initial_metadata || op->send_message
            || op->send_trailing_metadata) {
        if (trans->sock.getFd() < 0) {
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
void HomaListener::Transport::perform_op(grpc_transport* gt, grpc_transport_op* op)
{
    Transport *trans = containerOf(gt, &Transport::vtable);
    if (op->start_connectivity_watch) {
        trans->state_tracker.AddWatcher(
                op->start_connectivity_watch_state,
                std::move(op->start_connectivity_watch));
    }
    if (op->stop_connectivity_watch) {
        trans->state_tracker.RemoveWatcher(op->stop_connectivity_watch);
    }
    if (op->disconnect_with_error != GRPC_ERROR_NONE) {
        gpr_log(GPR_INFO, "HomaListener::Transport::perform_op got "
                "disconnect_with_error: %s",
                grpc_error_string(op->disconnect_with_error));
        GRPC_ERROR_UNREF(op->disconnect_with_error);
    }
    if (op->goaway_error != GRPC_ERROR_NONE) {
        gpr_log(GPR_INFO, "HomaListener::Transport::perform_op got goaway_error: %s",
                grpc_error_string(op->goaway_error));
        GRPC_ERROR_UNREF(op->goaway_error);
    }
    if (op->set_accept_stream) {
        grpc_core::MutexLock lock(&trans->mutex);
        trans->accept_stream_cb = op->set_accept_stream_fn;
        trans->accept_stream_data = op->set_accept_stream_user_data;
    }
    if (op->bind_pollset) {
        gpr_log(GPR_INFO, "HomaListener::Transport::perform_op invoked with "
                "bind_pollset");
    }
    if (op->bind_pollset_set) {
        gpr_log(GPR_INFO, "HomaListener::Transport::perform_op invoked with "
                "bind_pollset_set");
    }
    if (op->send_ping.on_initiate) {
        gpr_log(GPR_INFO, "HomaListener::Transport::perform_op invoked with "
                "send_ping.on_initiate");
    }
    if (op->send_ping.on_ack) {
        gpr_log(GPR_INFO, "HomaListener::Transport::perform_op invoked with "
                "send_ping.on_ack");
    }
    if (op->reset_connect_backoff) {
        gpr_log(GPR_INFO, "HomaListener::Transport::perform_op invoked with "
                "reset_connect_backoff");
    }

    grpc_core::ExecCtx::Run(DEBUG_LOCATION, op->on_consumed, GRPC_ERROR_NONE);
}

void HomaListener::Transport::destroy_stream(grpc_transport* gt, grpc_stream* gs,
        grpc_closure* closure)
{
    {
        Transport *trans = containerOf(gt, &Transport::vtable);
        HomaStream *stream = reinterpret_cast<HomaStream*>(gs);
        grpc_core::MutexLock lock(&trans->mutex);

        // This ensures that no-one else is using the stream while we destroy it.
        stream->mutex.Lock();
        stream->mutex.Unlock();
        trans->activeRpcs.erase(stream->streamId);
        stream->~HomaStream();
    }
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, closure, GRPC_ERROR_NONE);
}

void HomaListener::Transport::destroy(grpc_transport* gt)
{
    Transport *trans = containerOf(gt, &Transport::vtable);
    delete trans;
}

grpc_endpoint* HomaListener::Transport::get_endpoint(grpc_transport* gt)
{
    gpr_log(GPR_INFO, "HomaListener::Transport::get_endpoint invoked");
    return nullptr;
}
