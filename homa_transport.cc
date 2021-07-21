#include "homa_transport.h"
#include "homa.h"

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/connector.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
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

class HomaConnector : public grpc_core::SubchannelConnector {
public:
    void Connect(const Args& args, Result* result, grpc_closure* notify)
            override
    {
        gpr_log(GPR_INFO, "HomaConnector::Connect invoked");
    }
    void Shutdown(grpc_error_handle error) override
    {
        gpr_log(GPR_INFO, "HomaConnector::Shutdown invoked");
    }
};

/**
 * This class is invoked by gRPC to create subchannels for a channel.
 */
class HomaSubchannelFactory : public grpc_core::ClientChannelFactory {
public:
    grpc_core::RefCountedPtr<grpc_core::Subchannel> CreateSubchannel(
            const grpc_channel_args* args) override {
        gpr_log(GPR_INFO, "HomaSubchannelFactory::CreateSubchannel invoked");
        grpc_core::RefCountedPtr<grpc_core::Subchannel> s =
                grpc_core::Subchannel::Create(
                grpc_core::MakeOrphanable<HomaConnector>(), args);
        return s;
    }
};

// The following code is used for one-time initialization of a
// subchannel factory object.
HomaSubchannelFactory* homa_factory;
gpr_once homa_factory_once = GPR_ONCE_INIT;

void FactoryInit() {
  homa_factory = new HomaSubchannelFactory();
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
    
    gpr_once_init(&homa_factory_once, FactoryInit);
    to_add[2] = grpc_core::ClientChannelFactory::CreateChannelArg(
            homa_factory);
    
    const char* to_remove[] = {GRPC_ARG_SERVER_URI, to_add[2].key};
    grpc_channel_args* new_args = grpc_channel_args_copy_and_add_and_remove(
            args, to_remove, 2, to_add, 3);
    
    grpc_channel *channel = grpc_channel_create(target, new_args,
            GRPC_CLIENT_CHANNEL, reinterpret_cast<grpc_transport*>(transport));
    grpc_channel_args_destroy(new_args);
    
    return channel;
}

/**
 * Invoked by gRPC to initialize a new stream for Homa communication.
 * \param gt
 *      Pointer to a HomaTransport.
 * \param gs
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