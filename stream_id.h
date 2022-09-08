#ifndef RPC_ID_H
#define RPC_ID_H

#include<sys/socket.h>
#include<netinet/in.h>

#include "homa.h"

#include "src/core/lib/iomgr/resolve_address.h"

/**
 * Holds information identifying a stream (which represents a gRPC RPC)
 * in a form that can be used as a key in std::unordered_map.
 */
struct StreamId {
    // Holds a struct sockaddr specifying the address and port of the
    // other machine.
    sockaddr_in_union addr;
    
    // Unique id for this RPC among all those from its client.
    uint32_t id;

    StreamId() {}
    StreamId(uint32_t id);
    StreamId(const StreamId &other);
    StreamId(grpc_resolved_address *gaddr, uint32_t id);
    StreamId& operator=(const StreamId &other);
    bool operator==(const StreamId& other) const;

    std::string IpToString();
    
    int port()
    {
        if (addr.in6.sin6_family == AF_INET6) {
            return htons(addr.in6.sin6_port);
        } else if (addr.in4.sin_family == AF_INET) {
            return htons(addr.in4.sin_port);
        }
        return -1;
    }

    /**
     * This class computes a hash of an StreamId, so that StreamIds can
     * be used as keys in unordered_maps.
     */
    struct Hasher {
        std::size_t operator()(const StreamId& streamId) const
        {
            std::size_t hash = 0;
            if (streamId.addr.in6.sin6_family == AF_INET6) {
                hash ^= std::hash<int>()(streamId.addr.in6.sin6_addr.s6_addr32[0]);
                hash ^= std::hash<int>()(streamId.addr.in6.sin6_addr.s6_addr32[1]);
                hash ^= std::hash<int>()(streamId.addr.in6.sin6_addr.s6_addr32[2]);
                hash ^= std::hash<int>()(streamId.addr.in6.sin6_addr.s6_addr32[3]);
                hash ^= std::hash<short>()(streamId.addr.in6.sin6_port);
            } else if (streamId.addr.in4.sin_family == AF_INET) {
                hash ^= std::hash<int>()(streamId.addr.in4.sin_addr.s_addr);
                hash ^= std::hash<short>()(streamId.addr.in4.sin_port);
            }
            return hash;
        }
    };
};

#endif // RPC_WIRE_H
