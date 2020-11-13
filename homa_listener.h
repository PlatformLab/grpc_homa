#include <mutex>
#include <optional>
#include <unordered_map>

#include <grpcpp/grpcpp.h>
#include "src/core/lib/surface/server.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/iomgr/ev_posix.h"

/**
 * Stores all the state needed to serve Homa requests on a particular
 * port; owns the socket associated with that port.
 */
class HomaListener : public grpc_core::Server::ListenerInterface {
public:
    void Orphan() override ;
    void Start(grpc_core::Server* server,
            const std::vector<grpc_pollset*>* pollsets) override;
    static HomaListener *Get(int port);
    void SetOnDestroyDone(grpc_closure* on_destroy_done) override;
    grpc_core::channelz::ListenSocketNode* channelz_listen_socket_node()
            const override;
    ~HomaListener();
    
protected:
	HomaListener(int port);
    static void InitShared(void);
    static void OnRead(void* arg, grpc_error* err);
    
    /**
     * Information that is shared across all HomaListener objects.
     */
    struct Shared {
        // Contains pointers to all open Homa ports: keys are port numbers.
        std::unordered_map<int, HomaListener *> ports;

        // Synchronizes access to this structure.
        std::mutex mutex;
        
        Shared() : ports(), mutex() {}
    };
    
    // Homa port number managed by this object.
    int port;
	
    // File descriptor for a Homa socket.
    int fd;
    
    // Used by grpc to manage the socket in various ways, such as epoll.
    grpc_fd *gfd;
    
    // Used to call us back when fd is readable.
    grpc_closure read_closure;
    
    // Singleton object with common info.
    static std::optional<Shared> shared;
    
    // Used to synchronize initialization of shared.
    static gpr_once shared_once;
};
