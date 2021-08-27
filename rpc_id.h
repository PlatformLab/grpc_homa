#ifndef RPC_ID_H
#define RPC_ID_H

#include<sys/socket.h>
#include<netinet/in.h>

#include "src/core/lib/iomgr/resolve_address.h"

/**
 * Holds information identifying an RPC in a form that can be used as
 * a key in std::unordered_map.
 */
struct RpcId {
    // Holds a struct sockaddr specifying the address and port of the
    // other machine. Large enough to hold either an IPV4 or IPV6 address.
    char addr[sizeof(struct sockaddr_in6)];
    
    // Number of bytes in addr that are actually used.
    size_t addrSize;
    
    // Unique id for this RPC among all those from its client.
    uint32_t id;

    RpcId() {}
    RpcId(grpc_resolved_address *gaddr, uint32_t id);
    bool operator==(const RpcId& other) const;
    
    // Convenient accessors for part or all of addr.
    struct sockaddr *sockaddr()
    {
        return reinterpret_cast<struct sockaddr*>(&addr);
    }
    
    struct sockaddr_in *inaddr()
    {
        return reinterpret_cast<struct sockaddr_in *>(&addr);
    }
    
    int ipv4Addr()
    {
        return htonl(inaddr()->sin_addr.s_addr);
    }
    
    int port()
    {
        return htons(inaddr()->sin_port);
    }

    /**
     * This class computes a hash of an RpcId, so that RpcIds can
     * be used as keys in unordered_maps.
     */
    struct Hasher {
        std::size_t operator()(const RpcId& rpcId) const
        {
            std::size_t hash = 0;
            const int *ints = reinterpret_cast<const int*>(rpcId.addr);
            for (uint32_t i = 0; i+4 <= rpcId.addrSize; i = i+4, ints++) {
                hash ^= std::hash<int>()(*ints);
            }
            return hash;
        }
    };
};

#endif // RPC_WIRE_H