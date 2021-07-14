#include "homa_transport.h"
#include "homa.h"

#include "src/core/lib/surface/channel.h"

HomaTransport *HomaTransport::transport = nullptr;

static void log_metadata(const grpc_metadata_batch* md_batch, bool is_client,
                  bool is_initial) {
  for (grpc_linked_mdelem* md = md_batch->list.head; md != nullptr;
       md = md->next) {
    char* key = grpc_slice_to_c_string(GRPC_MDKEY(md->md));
    char* value = grpc_slice_to_c_string(GRPC_MDVALUE(md->md));
    gpr_log(GPR_INFO, "INPROC:%s:%s: %s: %s", is_initial ? "HDR" : "TRL",
            is_client ? "CLI" : "SVR", key, value);
    gpr_free(key);
    gpr_free(value);
  }
}

/**
 * Constructor for HomaTransports.
 * \param port
 *      The Homa port number that this object will manage.
 */
HomaTransport::HomaTransport()
    : base()
    , vtable()
    , streams()
    , fd(-1)
{
    vtable.sizeof_stream =       sizeof(Stream);
    vtable.name =                "homa";
    vtable.init_stream =         init_stream;
    vtable.set_pollset =         set_pollset;
    vtable.set_pollset_set =     set_pollset_set;
    vtable.perform_stream_op =   perform_stream_op;
    vtable.perform_op =          perform_op;
    vtable.destroy_stream =      destroy_stream;
    vtable.destroy =             destroy;
    vtable.get_endpoint =        get_endpoint;
    base.vtable = &vtable;
    
    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_HOMA);
	if (fd < 0) {
        gpr_log(GPR_ERROR, "Couldn't open Homa socket: %s", strerror(errno));
	}
}

HomaTransport::~HomaTransport()
{
}

/**
 * Create a new Homa channel.
 * \param target
 *      Describes the peer this channel should connect to.
 * \param args
 *      Various arguments for the new channel.
 */
grpc_channel *HomaTransport::create_channel(const char* target,
            const grpc_channel_args* args)
{
    gpr_log(GPR_INFO, "Creating channel for %s", target);
    grpc_core::ExecCtx exec_ctx;
    
    if (!transport) {
        transport = new HomaTransport();
    }

    // A default authority channel argument is necessary to keep gRPC happy.
    grpc_arg default_authority_arg;
    default_authority_arg.type = GRPC_ARG_STRING;
    default_authority_arg.key = (char *) GRPC_ARG_DEFAULT_AUTHORITY;
    default_authority_arg.value.string = (char *) "homa.authority";
    grpc_channel_args* args2 =
            grpc_channel_args_copy_and_add(args, &default_authority_arg, 1);
    
    grpc_channel *channel = grpc_channel_create(target, args2,
            GRPC_CLIENT_DIRECT_CHANNEL,
            reinterpret_cast<grpc_transport*>(transport));
    grpc_channel_args_destroy(args2);
    
    return channel;
}

/**
 * Invoked by gRPC to initialize a new stream for Homa communication.
 * \param self
 *      Pointer to a HomaTransport.
 * \param stream
 *      Block of memory in which to initialize a stream.
 * \param refcount
 *      Externally owned reference to associate with the stream.
 * \param server_data
 *      No idea what this is.
 * \param arena
 *      No idea what this is.
 * 
 * \return
 *      Zero means success, nonzero means failure.
 */
int HomaTransport::init_stream(grpc_transport* gt, grpc_stream* gs,
        grpc_stream_refcount* refcount, const void* server_data,
        grpc_core::Arena* arena)
{
    HomaTransport* ht = reinterpret_cast<HomaTransport*>(gt);
    Stream *stream = reinterpret_cast<Stream *>(gs);
    new (stream) Stream(ht, refcount, arena);
    ht->streams.push_back(stream);
    gpr_log(GPR_INFO, "HomaTransport::init_stream invoked");
    return 0;
}

void HomaTransport::set_pollset(grpc_transport* gt, grpc_stream* gs,
        grpc_pollset* pollset)
{
    gpr_log(GPR_INFO, "HomaTransport::set_pollset invoked");
    
}

void HomaTransport::set_pollset_set(grpc_transport* gt, grpc_stream* gs,
        grpc_pollset_set* pollset_set)
{
    gpr_log(GPR_INFO, "HomaTransport::set_pollset_set invoked");
    
}

void HomaTransport::perform_stream_op(grpc_transport* gt, grpc_stream* gs,
        grpc_transport_stream_op_batch* op)
{
    gpr_log(GPR_INFO, "HomaTransport::perform_stream_op invoked");
    if (op->send_initial_metadata) {
        gpr_log(GPR_INFO, "HomaTransport::perform_stream_op: send "
                "initial metadata");
        log_metadata(op->payload->send_initial_metadata.send_initial_metadata,
                true, true);
    }
    if (op->send_message) {
        gpr_log(GPR_INFO, "HomaTransport::perform_stream_op: send message");
        
    }
    if (op->send_trailing_metadata) {
        gpr_log(GPR_INFO, "HomaTransport::perform_stream_op: send "
                "trailing metadata");
        log_metadata(op->payload->send_trailing_metadata.send_trailing_metadata,
                true, true);
    }
    if (op->cancel_stream) {
        gpr_log(GPR_INFO, "HomaTransport::perform_stream_op: cancel stream");
    }
    if (op->recv_initial_metadata) {
        gpr_log(GPR_INFO, "HomaTransport::perform_stream_op: "
                "receive initial metadata");
    }
    if (op->recv_message) {
        gpr_log(GPR_INFO, "HomaTransport::perform_stream_op: receive message");
    }
    if (op->recv_trailing_metadata) {
        gpr_log(GPR_INFO, "HomaTransport::perform_stream_op: "
                "receive trailing metadata");
    }
    if (op->on_complete) {
        gpr_log(GPR_INFO, "HomaTransport::perform_stream_op: got "
                "on_complete closure");
    }
}

void HomaTransport::perform_op(grpc_transport* gt, grpc_transport_op* op)
{
    gpr_log(GPR_INFO, "HomaTransport::perform_op invoked");
    
}

void HomaTransport::destroy_stream(grpc_transport* gt, grpc_stream* gs,
        grpc_closure* then_schedule_closure)
{
    gpr_log(GPR_INFO, "HomaTransport::destroy_stream invoked");
    
}

void HomaTransport::destroy(grpc_transport* gt)
{
    gpr_log(GPR_INFO, "HomaTransport::destroy invoked");
    
}

grpc_endpoint* HomaTransport::get_endpoint(grpc_transport* gt)
{
    gpr_log(GPR_INFO, "HomaTransport::get_endpoint invoked");
    return nullptr;
}