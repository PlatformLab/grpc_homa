#include <arpa/inet.h>

#include "homa_transport.h"
#include "homa.h"
#include "wire.h"

#include <grpc/impl/codegen/slice.h>

#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/lib/surface/channel.h"

HomaClient *HomaClient::sharedClient = nullptr;
HomaClient::SubchannelFactory* HomaClient::factory = nullptr;
gpr_once homa_once = GPR_ONCE_INIT;

static void log_metadata(const grpc_metadata_batch* md_batch, bool is_client,
        bool is_initial)
{
    for (grpc_linked_mdelem* md = md_batch->list.head; md != nullptr;
            md = md->next) {
        char* key = grpc_slice_to_c_string(GRPC_MDKEY(md->md));
        char* value = grpc_slice_to_c_string(GRPC_MDVALUE(md->md));
        gpr_log(GPR_INFO, "%s:%s: %s: %s", is_initial ? "HDR" : "TRL",
                is_client ? "CLI" : "SVR", key, value);
        gpr_free(key);
        gpr_free(value);
    }
}

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
 * Since Homa doesn't use connections, there's not much to do here.
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
    gpr_log(GPR_INFO, "HomaConnector::Connect invoked");
    grpc_resolved_address addr;
    grpc_core::Subchannel::GetAddressFromSubchannelAddressArg(
            args.channel_args, &addr);
    printf("Address is %u bytes long\n", addr.len);
    result->Reset();
    result->transport = &(new HomaClient::Peer(HomaClient::sharedClient,
            &addr))->base;
    result->channel_args = args.channel_args;

    // Notify immediately, since there's no connection to create.
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, notify, GRPC_ERROR_NONE);
}

HomaClient::Peer::Peer(HomaClient *hc, grpc_resolved_address *addr)
        : base()
        , hc(hc)
        , addr(*addr)
{
    base.vtable = &hc->vtable;
}

void HomaClient::Connector::Shutdown(grpc_error_handle error)
{
    gpr_log(GPR_INFO, "HomaConnector::Shutdown invoked");
}

/**
 * Perform one-time initialization for client-side Homa code.
 */
void HomaClient::init() {
    sharedClient = new HomaClient();
    factory = new SubchannelFactory();
}

/**
 * Constructor for HomaTransports.
 * \param port
 *      The Homa port number that this object will manage.
 */
HomaClient::HomaClient()
    : vtable()
    , streams()
    , fd(-1)
    , gfd(nullptr)
    , read_closure()
    , nextId(1)
{
    vtable.sizeof_stream =       sizeof(Stream);
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
        GRPC_CLOSURE_INIT(&read_closure, onRead, this,
                grpc_schedule_on_exec_ctx);
        grpc_fd_notify_on_read(gfd, &read_closure);
    }
}

HomaClient::~HomaClient()
{
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
    
    gpr_once_init(&homa_once, init);

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
    
    to_add[2] = grpc_core::ClientChannelFactory::CreateChannelArg(factory);
    
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
 *      No idea what this is.
 * 
 * \return
 *      Zero means success, nonzero means failure.
 */
int HomaClient::init_stream(grpc_transport* gt, grpc_stream* gs,
        grpc_stream_refcount* refcount, const void* server_data,
        grpc_core::Arena* arena)
{
    Peer *peer = reinterpret_cast<Peer*>(gt);
    HomaClient* hc = peer->hc;
    Stream *stream = reinterpret_cast<Stream *>(gs);
    new (stream) Stream(peer, refcount, arena);
    hc->streams.push_back(stream);
    gpr_log(GPR_INFO, "HomaClient::init_stream invoked");
    return 0;
}

void HomaClient::set_pollset(grpc_transport* gt, grpc_stream* gs,
        grpc_pollset* pollset)
{
    Peer *peer = reinterpret_cast<Peer*>(gt);
    HomaClient* hc = peer->hc;
    
    if (hc->gfd) {
        grpc_pollset_add_fd(pollset, hc->gfd);
    }
    gpr_log(GPR_INFO, "HomaClient::set_pollset invoked");
    
}

void HomaClient::set_pollset_set(grpc_transport* gt, grpc_stream* gs,
        grpc_pollset_set* pollset_set)
{
    Peer *peer = reinterpret_cast<Peer*>(gt);
    HomaClient* hc = peer->hc;
    
    if (hc->gfd) {
        grpc_pollset_set_add_fd(pollset_set, hc->gfd);
    }
    gpr_log(GPR_INFO, "HomaClient::set_pollset_set invoked");
    
}

void HomaClient::perform_stream_op(grpc_transport* gt, grpc_stream* gs,
        grpc_transport_stream_op_batch* op)
{
    Peer *peer = reinterpret_cast<Peer*>(gt);
    Stream *stream = reinterpret_cast<Stream*>(gs);
    HomaClient *hc = peer->hc;
    
    gpr_log(GPR_INFO, "HomaClient::perform_stream_op invoked");
    if (op->send_initial_metadata) {
        gpr_log(GPR_INFO, "HomaClient::perform_stream_op: send "
                "initial metadata");
        log_metadata(op->payload->send_initial_metadata.send_initial_metadata,
                true, true);
    }
    if (op->send_message) {
        gpr_log(GPR_INFO, "HomaClient::perform_stream_op: send message");
        HomaMessage msg;
        msg.id = htonl(hc->nextId);
        hc->nextId++;
        uint32_t length = op->payload->send_message.send_message->length();
        msg.payloadLength = htonl(length);
        uint32_t offset = 0;
        grpc_slice slice;
        while (offset < length) {
            if (!op->payload->send_message.send_message->Next(length,
                    nullptr)) {
                /* Should never reach here */
                GPR_ASSERT(false);
            }
            if (op->payload->send_message.send_message->Pull(&slice)
                    != GRPC_ERROR_NONE) {
                /* Should never reach here */
                GPR_ASSERT(false);
            }
            memcpy(msg.payload + offset, GRPC_SLICE_START_PTR(slice),
                    GRPC_SLICE_LENGTH(slice));
            offset += GRPC_SLICE_LENGTH(slice);
            grpc_slice_unref(slice);
            gpr_log(GPR_INFO, "Copied %lu bytes into message buffer",
                    GRPC_SLICE_LENGTH(slice));
        }
        if (hc->fd < 0) {
            stream->error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                    "Couldn't open Homa socket");
            goto sendMessageDone;
        }
        uint64_t homaId;
        int status = homa_send(hc->fd, &msg, sizeof(msg) - (10000 - length),
                reinterpret_cast<struct sockaddr *>(&peer->addr),
                peer->addr.len, &homaId);
        if (status < 0) {
                stream->error = GRPC_OS_ERROR(errno,
                        "Couldn't send Homa message");
                goto sendMessageDone;
        }
        gpr_log(GPR_INFO, "Sent Homa message with %u payload bytes", length);
    }
    sendMessageDone:
    if (op->send_trailing_metadata) {
        gpr_log(GPR_INFO, "HomaClient::perform_stream_op: send "
                "trailing metadata");
        log_metadata(op->payload->send_trailing_metadata.send_trailing_metadata,
                true, true);
    }
    if (op->cancel_stream) {
        gpr_log(GPR_INFO, "HomaClient::perform_stream_op: cancel "
                "stream ((%s)", grpc_error_std_string(
                op->payload->cancel_stream.cancel_error).c_str());
    }
    if (op->recv_initial_metadata) {
        gpr_log(GPR_INFO, "HomaClient::perform_stream_op: "
                "receive initial metadata");
        if (stream->error != GRPC_ERROR_NONE) {
            grpc_core::ExecCtx::Run(DEBUG_LOCATION, op->payload->
                    recv_initial_metadata.recv_initial_metadata_ready,
                    GRPC_ERROR_REF(stream->error));
        }
    }
    if (op->recv_message) {
        gpr_log(GPR_INFO, "HomaClient::perform_stream_op: receive message");
        if (stream->error != GRPC_ERROR_NONE) {
            grpc_core::ExecCtx::Run(DEBUG_LOCATION,
                    op->payload->recv_message.recv_message_ready,
                    GRPC_ERROR_REF(stream->error));
        }
    }
    if (op->recv_trailing_metadata) {
        gpr_log(GPR_INFO, "HomaClient::perform_stream_op: "
                "receive trailing metadata");
        if (stream->error != GRPC_ERROR_NONE) {
            grpc_core::ExecCtx::Run(DEBUG_LOCATION, op->payload->
                    recv_trailing_metadata.recv_trailing_metadata_ready,
                    GRPC_ERROR_REF(stream->error));
        }
    }
    if (op->on_complete) {
        grpc_core::ExecCtx::Run(DEBUG_LOCATION, op->on_complete,
                GRPC_ERROR_REF(stream->error));
    }
}

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

void HomaClient::destroy_stream(grpc_transport* gt, grpc_stream* gs,
        grpc_closure* then_schedule_closure)
{
    Stream *stream = reinterpret_cast<Stream*>(gs);
    
    gpr_log(GPR_INFO, "HomaClient::destroy_stream invoked");
    GRPC_ERROR_UNREF(stream->error);
}

void HomaClient::destroy(grpc_transport* gt)
{
    gpr_log(GPR_INFO, "HomaClient::destroy invoked");
    
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
    gpr_log(GPR_INFO, "HomaClient::onRead invoked");
    
    if (error != GRPC_ERROR_NONE) {
        gpr_log(GPR_ERROR, "HomaClient::onRead invoked with error: %s",
                grpc_error_string(error));
    }
    char buffer[10000];
    struct sockaddr peerAddr;
    uint64_t homaId = 0;
    ssize_t length = homa_recv(hc->fd, buffer, sizeof(buffer),
            HOMA_RECV_RESPONSE|HOMA_RECV_NONBLOCKING, &peerAddr,
            sizeof(peerAddr), &homaId);
    if (length < 0) {
        if (errno == EAGAIN) {
            return;
        }
        gpr_log(GPR_ERROR, "Error in Homa recv for id %lu: %s",
                homaId, strerror(errno));
        return;
    }
    gpr_log(GPR_INFO, "HomaClient::onRead received message with %lu bytes",
            length);
}