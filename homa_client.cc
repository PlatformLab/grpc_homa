#include <arpa/inet.h>

#include "homa_client.h"
#include "homa_incoming.h"
#include "homa.h"
#include "util.h"
#include "wire.h"

#include <grpc/impl/codegen/slice.h>

#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/lib/surface/channel.h"

HomaClient::SubchannelFactory HomaClient::factory;
HomaClient *HomaClient::sharedClient = nullptr;
grpc_core::Mutex HomaClient::refCountMutex;

/**
 * Creates a new subchannel for an existing channel.
 * \param args
 *      Arguments associated with the channel.
 */
grpc_core::RefCountedPtr<grpc_core::Subchannel>
HomaClient::SubchannelFactory::CreateSubchannel(const grpc_channel_args* args)
{
    gpr_log(GPR_INFO, "HomaSubchannelFactory::CreateSubchannel invoked");
    grpc_core::RefCountedPtr<grpc_core::Subchannel> s =
            grpc_core::Subchannel::Create(
            grpc_core::MakeOrphanable<HomaClient::Connector>(), args);
    return s;
}

/**
 * This method is invoked to create a connection to a specific peer.
 * \param args
 *      Various arguments for setting up the connection, such as
 *      the arguments for the associated subchannel.
 * \param result
 *      Information about the connection is returned here.
 * \param notify
 *      Closure to invoke once the connection has been established.
 */
void HomaClient::Connector::Connect(const HomaClient::Connector::Args& args,
        HomaClient::Connector::Result* result, grpc_closure* notify)
{
    // Homa doesn't use connections, so there isn't much to do here.
    gpr_log(GPR_INFO, "HomaConnector::Connect invoked");
    grpc_resolved_address addr;
    grpc_core::Subchannel::GetAddressFromSubchannelAddressArg(
            args.channel_args, &addr);
    printf("Address is %u bytes long\n", addr.len);
    result->Reset();
    {
        grpc_core::MutexLock lock(&refCountMutex);
        if (sharedClient == nullptr) {
            sharedClient = new HomaClient();
            Wire::init();
        }
        sharedClient->numPeers++;
    }
    result->transport = &(new HomaClient::Peer(HomaClient::sharedClient,
            &addr))->transport;
    result->channel_args = grpc_channel_args_copy(args.channel_args);

    // Notify immediately, since there's no connection to create.
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, notify, GRPC_ERROR_NONE);
}

HomaClient::Peer::Peer(HomaClient *hc, grpc_resolved_address *addr)
        : transport()
        , hc(hc)
        , addr(*addr)
{
    transport.vtable = &hc->vtable;
}

/**
 * Invoked to cancel any in-flight connections and cleanup the connector.
 * \param error
 *      Describes the problem (if any) that led to the shutdown;
 *      ownership passes to us.
 */
void HomaClient::Connector::Shutdown(grpc_error_handle error)
{
    // Nothing to do here.
    GRPC_ERROR_UNREF(error);
}

/**
 * Constructor for HomaTransports.
 * \param port
 *      The Homa port number that this object will manage.
 */
HomaClient::HomaClient()
    : vtable()
    , streams()
    , nextId(1)
    , mutex()
    , fd(-1)
    , gfd(nullptr)
    , readClosure()
    , numPeers(0)
{
    vtable.sizeof_stream =       sizeof(HomaStream);
    vtable.name =                "homa_client";
    vtable.init_stream =         init_stream;
    vtable.set_pollset =         set_pollset;
    vtable.set_pollset_set =     set_pollset_set;
    vtable.perform_stream_op =   perform_stream_op;
    vtable.perform_op =          perform_op;
    vtable.destroy_stream =      destroy_stream;
    vtable.destroy =             destroy;
    vtable.get_endpoint =        get_endpoint;
    
    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_HOMA);
	if (fd < 0) {
        gpr_log(GPR_ERROR, "Couldn't open Homa socket: %s", strerror(errno));
	} else {
        gfd = grpc_fd_create(fd, "homa-socket", true);
        GRPC_CLOSURE_INIT(&readClosure, onRead, this,
                grpc_schedule_on_exec_ctx);
        grpc_fd_notify_on_read(gfd, &readClosure);
    }
}

HomaClient::~HomaClient()
{
    gpr_log(GPR_INFO, "HomaClient destructor invoked");
    grpc_fd_shutdown(gfd,
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("Destroying HomaClient"));
    grpc_fd_orphan(gfd, nullptr, nullptr, "Destroying HomaClient");
}

/**
 * Create a new Homa channel.
 * \param target
 *      Describes the peer this channel should connect to.
 * \param args
 *      Various arguments for the new channel.
 */
grpc_channel *HomaClient::create_channel(const char* target,
            const grpc_channel_args* args)
{
    gpr_log(GPR_INFO, "Creating channel for %s", target);
    grpc_core::ExecCtx exec_ctx;

    // Add 3 channel arguments:
    // * Default authority channel argument (to keep gRPC happy)
    // * Server URI
    // * Subchannel factory
    grpc_arg to_add[3];
    to_add[0].type = GRPC_ARG_STRING;
    to_add[0].key = (char *) GRPC_ARG_DEFAULT_AUTHORITY;
    to_add[0].value.string = (char *) "homa.authority";
    
    grpc_core::UniquePtr<char> canonical_target =
            grpc_core::ResolverRegistry::AddDefaultPrefixIfNeeded(target);
    to_add[1] = grpc_channel_arg_string_create(
            const_cast<char*>(GRPC_ARG_SERVER_URI), canonical_target.get());
    
    to_add[2] = grpc_core::ClientChannelFactory::CreateChannelArg(&factory);
    
    const char* to_remove[] = {GRPC_ARG_SERVER_URI, to_add[2].key};
    grpc_channel_args* new_args = grpc_channel_args_copy_and_add_and_remove(
            args, to_remove, 2, to_add, 3);
    
    grpc_channel *channel = grpc_channel_create(target, new_args,
            GRPC_CLIENT_CHANNEL, nullptr);
    grpc_channel_args_destroy(new_args);
    
    return channel;
}

/**
 * Invoked by gRPC to initialize a new stream for Homa communication.
 * \param gt
 *      Pointer to a HomaPeer that identifies the target.
 * \param gs
 *      Block of memory in which to initialize a stream.
 * \param refcount
 *      Externally owned reference to associate with the stream.
 * \param server_data
 *      Not used.
 * \param arena
 *      Use this for allocating storage for use by the stream (will
 *      be freed when the stream is destroyed).
 * 
 * \return
 *      Zero means success, nonzero means failure.
 */
int HomaClient::init_stream(grpc_transport* gt, grpc_stream* gs,
        grpc_stream_refcount* refcount, const void* server_data,
        grpc_core::Arena* arena)
{
    Peer *peer = containerOf(gt, &HomaClient::Peer::transport);
    HomaClient* hc = peer->hc;
    HomaStream *stream = reinterpret_cast<HomaStream *>(gs);
    grpc_core::MutexLock lock(&hc->mutex);
    uint32_t id = hc->nextId;
    hc->nextId++;
    new (stream) HomaStream(StreamId(&peer->addr, id), 0, hc->fd, refcount, arena);
    hc->streams.emplace(stream->streamId, stream);
    gpr_log(GPR_INFO, "HomaClient::init_stream invoked");
    return 0;
}

/**
 * Invoked by gRPC to add any file descriptors for a transport to
 * a pollset so that we'll get callbacks when messages arrive.
 * \param gt
 *      Identifies a particular Peer.
 * \param gs
 *      Info for a particular RPC.
 * \param pollset
 *      Where to add polling information.
 */
void HomaClient::set_pollset(grpc_transport* gt, grpc_stream* gs,
        grpc_pollset* pollset)
{
    Peer *peer = containerOf(gt, &HomaClient::Peer::transport);
    HomaClient* hc = peer->hc;
    
    if (hc->gfd) {
        grpc_pollset_add_fd(pollset, hc->gfd);
    }
    gpr_log(GPR_INFO, "HomaClient::set_pollset invoked");
    
}

/**
 * Invoked by gRPC to add any file descriptors for a transport to
 * a pollset_set so that we'll get callbacks when messages arrive.
 * \param gt
 *      Identifies a particular Peer.
 * \param gs
 *      Info for a particular RPC.
 * \param pollset_set
 *      Where to add polling information.
 */
void HomaClient::set_pollset_set(grpc_transport* gt, grpc_stream* gs,
        grpc_pollset_set* pollset_set)
{
    Peer *peer = containerOf(gt, &HomaClient::Peer::transport);
    HomaClient* hc = peer->hc;
    
    if (hc->gfd) {
        grpc_pollset_set_add_fd(pollset_set, hc->gfd);
    }
    gpr_log(GPR_INFO, "HomaClient::set_pollset_set invoked");
    
}

/**
 * This is the main method invoked by gRPC while processing an RPC.
 * Is invoked multiple times over the lifetime of the RPC.
 * \param gt
 *      Identifies the server for the RPC.
 * \param gs
 *      State info about the RPC.
 * \param op
 *      Describes one or more operations to perform on the stream.
 */
void HomaClient::perform_stream_op(grpc_transport* gt, grpc_stream* gs,
        grpc_transport_stream_op_batch* op)
{
    HomaStream *stream = reinterpret_cast<HomaStream*>(gs);
    grpc_core::MutexLock lock(&stream->mutex);
    
    gpr_log(GPR_INFO, "HomaClient::perform_stream_op invoked");
    if (op->send_initial_metadata || op->send_message
            || op->send_trailing_metadata) {
        stream->xmit(op);
    }

    if (op->cancel_stream) {
        gpr_log(GPR_INFO, "HomaClient::perform_stream_op: cancel "
                "stream ((%s)", grpc_error_std_string(
                op->payload->cancel_stream.cancel_error).c_str());
    }
    if (op->recv_initial_metadata || op->recv_message
            || op->recv_trailing_metadata) {
        stream->saveCallbacks(op);
    }
    if (op->on_complete) {
        grpc_core::ExecCtx::Run(DEBUG_LOCATION, op->on_complete,
                GRPC_ERROR_REF(stream->error));
    }
}

/**
 * Invoked by gRPC to perform various operations on a transport (i.e. Peer).
 * \param gt
 *      The peer to manipulate.
 * \param op
 *      What to do on the peer.
 */
void HomaClient::perform_op(grpc_transport* gt, grpc_transport_op* op)
{
    gpr_log(GPR_INFO, "HomaClient::perform_op invoked");
    if (op->start_connectivity_watch) {
        gpr_log(GPR_INFO, "HomaClient::perform_op invoked with "
                "start_connectivity_watch");
    }
    if (op->stop_connectivity_watch) {
        gpr_log(GPR_INFO, "HomaClient::perform_op invoked with "
                "stop_connectivity_watch");
    }
    if (op->disconnect_with_error != GRPC_ERROR_NONE) {
        gpr_log(GPR_INFO, "HomaClient::perform_op got "
                "disconnect_with_error: %s",
                grpc_error_string(op->disconnect_with_error));
        GRPC_ERROR_UNREF(op->disconnect_with_error);
    }
    if (op->goaway_error != GRPC_ERROR_NONE) {
        gpr_log(GPR_INFO, "HomaClient::perform_op got goaway_error: %s",
                grpc_error_string(op->goaway_error));
        GRPC_ERROR_UNREF(op->goaway_error);
    }
    if (op->set_accept_stream) {
        gpr_log(GPR_INFO, "HomaClient::perform_op invoked with "
                "set_accept_stream");
    }
    if (op->bind_pollset) {
        gpr_log(GPR_INFO, "HomaClient::perform_op invoked with "
                "bind_pollset");
    }
    if (op->bind_pollset_set) {
        gpr_log(GPR_INFO, "HomaClient::perform_op invoked with "
                "bind_pollset_set");
    }
    if (op->send_ping.on_initiate) {
        gpr_log(GPR_INFO, "HomaClient::perform_op invoked with "
                "send_ping.on_initiate");
    }
    if (op->send_ping.on_ack) {
        gpr_log(GPR_INFO, "HomaClient::perform_op invoked with "
                "send_ping.on_ack");
    }
    if (op->reset_connect_backoff) {
        gpr_log(GPR_INFO, "HomaClient::perform_op invoked with "
                "reset_connect_backoff");
    }

    grpc_core::ExecCtx::Run(DEBUG_LOCATION, op->on_consumed, GRPC_ERROR_NONE);
}

/**
 * Destructor for Streams.
 * \param gt
 *      Transport (Peer) associated with the stream.
 * \param gs
 *      HomaStream to destroy.
 * \param closure
 *      Invoke this once destruction is complete.
 */
void HomaClient::destroy_stream(grpc_transport* gt, grpc_stream* gs,
        grpc_closure* closure)
{
    Peer *peer = containerOf(gt, &HomaClient::Peer::transport);
    HomaClient *hc = peer->hc;
    HomaStream *stream = reinterpret_cast<HomaStream*>(gs);
    
    gpr_log(GPR_INFO, "HomaClient::destroy_stream invoked");
    {
        grpc_core::MutexLock lock(&hc->mutex);
        hc->streams.erase(stream->streamId);
    }
    {
        grpc_core::MutexLock lock(&stream->mutex);
        stream->~HomaStream();
    }
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, closure, GRPC_ERROR_NONE);
}

/**
 * Destructor for transports (Peers).
 * \param gt
 *      Transport to destroy.
 */
void HomaClient::destroy(grpc_transport* gt)
{
    gpr_log(GPR_INFO, "HomaClient::destroy invoked");
    Peer *peer = containerOf(gt, &HomaClient::Peer::transport);
    delete peer;
    {
        grpc_core::MutexLock lock(&refCountMutex);
        sharedClient->numPeers--;
        if (sharedClient->numPeers == 0) {
            delete sharedClient;
            sharedClient = nullptr;
        }
    }
    
}

grpc_endpoint* HomaClient::get_endpoint(grpc_transport* gt)
{
    gpr_log(GPR_INFO, "HomaClient::get_endpoint invoked");
    return nullptr;
}

/**
 * Used in a closure that is invoked whenever the Homa socket becomes
 * readable.
 * \param arg
 *      Pointer to the HomaClient structure associated with the socket.
 * \param error
 *      Indicates whether the socket has an error condition.
 * 
 */
void HomaClient::onRead(void* arg, grpc_error* error)
{
    HomaClient *hc = static_cast<HomaClient*>(arg);
    HomaIncoming::UniquePtr msg;
    
    if (error != GRPC_ERROR_NONE) {
        gpr_log(GPR_ERROR, "HomaClient::onRead invoked with error: %s",
                grpc_error_string(error));
        return;
    }
    msg = HomaIncoming::read(hc->fd, HOMA_RECV_RESPONSE|HOMA_RECV_NONBLOCKING);
    grpc_fd_notify_on_read(hc->gfd, &hc->readClosure);
    if (!msg) {
        return;
    }
    
    HomaStream *stream;
    std::optional<grpc_core::MutexLock> streamLock;
    try {
        grpc_core::MutexLock lock(&hc->mutex);
        stream = hc->streams.at(msg->getStreamId());
        streamLock.emplace(&stream->mutex);
    } catch (std::out_of_range& e) {
        gpr_log(GPR_ERROR, "Ignoring response for unknown RPC id %d",
                msg->getStreamId().id);
        return;
    }
    stream->handleIncoming(std::move(msg));
    gpr_log(GPR_INFO, "HomaClient::onRead done");
}