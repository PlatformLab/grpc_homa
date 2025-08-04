#ifndef STREAM_ID_H
#define STREAM_ID_H

#include<sys/socket.h>
#include<netinet/in.h>

#include "homa.h"

#include "src/core/lib/iomgr/resolve_address.h"

/**
 * union sockaddr_in_union - Holds either an IPv4 or IPv6 address (smaller
 * and easier to use than sockaddr_storage).
 */
union sockaddr_in_union {
	/** @sa: Used to access as a generic sockaddr. */
	struct sockaddr sa;

	/** @in4: Used to access as IPv4 socket. */
	struct sockaddr_in in4;

	/** @in6: Used to access as IPv6 socket.  */
	struct sockaddr_in6 in6;
};

/**
 * Holds information identifying a stream (which represents a gRPC RPC)
 * in a form that can be used as a key in std::unordered_map.
 */
struct StreamId {
    // Specifies the address and port of the other machine.
    sockaddr_in_union addr;

    // Unique id for this RPC among all those from its client.
    uint32_t id;

    StreamId() {}
    StreamId(uint32_t id);
    StreamId(const StreamId &other);
    StreamId(grpc_resolved_address *gaddr, uint32_t id);
    StreamId& operator=(const StreamId &other);
    bool operator==(const StreamId& other) const;

    std::string toString();

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
            return hash ^ streamId.id;
        }
    };

    struct Pred {
        bool operator()(const StreamId& a, const StreamId& b) const
        {
            if (a.id != b.id) {
                return false;
            }
            if (a.addr.in6.sin6_family != b.addr.in6.sin6_family) {
                return false;
            }
            if (a.addr.in6.sin6_family == AF_INET6) {
                return (a.addr.in6.sin6_addr.s6_addr32[0]
                        == b.addr.in6.sin6_addr.s6_addr32[0])
                        && (a.addr.in6.sin6_addr.s6_addr32[1]
                        == b.addr.in6.sin6_addr.s6_addr32[1])
                        && (a.addr.in6.sin6_addr.s6_addr32[2]
                        == b.addr.in6.sin6_addr.s6_addr32[2])
                        && (a.addr.in6.sin6_addr.s6_addr32[3]
                        == b.addr.in6.sin6_addr.s6_addr32[3])
                        && (a.addr.in6.sin6_port == b.addr.in6.sin6_port);
            } else if (a.addr.in4.sin_family == AF_INET) {
                return (a.addr.in4.sin_addr.s_addr
                        == b.addr.in4.sin_addr.s_addr)
                        && (a.addr.in4.sin_port == b.addr.in4.sin_port);
            }
            return false;
        }
    };
};

#endif // STREAM_ID_H
