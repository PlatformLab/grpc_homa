#include <arpa/inet.h>

#include "stream_id.h"

/**
 * Construct an StreamId from a gRPC address.
 * \param gaddr
 *      The address of the peer for the RPC.
 * \param id
 *      Unique id assigned to this RPC by the client.
 */
StreamId::StreamId(grpc_resolved_address *gaddr, uint32_t id)
    : addr()
    , id(id)
{
    GPR_ASSERT(gaddr->len <= sizeof(addr));
    memset(reinterpret_cast<void*>(&addr.in6), 0, sizeof(addr.in6));
    memcpy(reinterpret_cast<void*>(&addr.in6), gaddr->addr, gaddr->len);
}

/**
 * Constructor for testing.
 * \param id
 *      Identifier for this particular RPC.
 */
StreamId::StreamId(uint32_t id)
    : addr()
    , id(id)
{
    addr.in6.sin6_family = AF_UNSPEC;
    addr.in6.sin6_addr = in6addr_any;
    addr.in6.sin6_port = htons(40);
}

StreamId::StreamId(const StreamId &other)
{
    addr.in6 = other.addr.in6;
    id = other.id;
}

StreamId& StreamId::operator=(const StreamId &other)
{
    addr.in6 = other.addr.in6;
    id = other.id;
    return *this;
}

/**
 * Comparison operator for StreamIds.
 * \param other
 *      StreamId to compare against
 * \return
 *      True means the StreamIds match, false means they don't.
 */
bool StreamId::operator==(const StreamId& other) const
{
    if (addr.in6.sin6_family == AF_INET6) {
        return (id == other.id) &&
            (bcmp(reinterpret_cast<const void*>(&addr.in6),
                  reinterpret_cast<const void*>(&other.addr.in6),
                  sizeof(struct sockaddr_in6)) == 0);
    } else {
        return (id == other.id) &&
            (bcmp(reinterpret_cast<const void*>(&addr.in4),
                  reinterpret_cast<const void*>(&other.addr.in4),
                  sizeof(struct sockaddr_in)) == 0);
    }
}

std::string StreamId::IpToString() {
    char buf[128] = {};
    if (addr.in6.sin6_family == AF_INET6) {
        if (inet_ntop(AF_INET6, &addr.in6.sin6_addr, buf, sizeof(buf))) {
            return buf;
        }
    }
    if (addr.in4.sin_family == AF_INET) {
        if (inet_ntop(AF_INET, &addr.in4.sin_addr, buf, sizeof(buf))) {
            return buf;
        }
    }
    return "invalid address";
}
