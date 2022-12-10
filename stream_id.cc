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
    memset(&addr, 0, sizeof(addr));
    memcpy(&addr, gaddr->addr, gaddr->len);
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
    addr.in6.sin6_family = AF_INET6;
    addr.in6.sin6_addr = in6addr_any;
    addr.in6.sin6_port = htons(40);
}

StreamId::StreamId(const StreamId &other)
{
    addr = other.addr;
    id = other.id;
}

StreamId& StreamId::operator=(const StreamId &other)
{
    addr = other.addr;
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
                (bcmp(&addr.in6, &other.addr.in6, sizeof(addr.in6)) == 0);
    } else {
        return (id == other.id) &&
            (bcmp(&addr.in4, &other.addr.in4, sizeof(addr.in4)) == 0);
    }
}

/**
 * Return a string containing all of the information in this struct.
 */
std::string StreamId::toString() {
    char buf[INET6_ADDRSTRLEN];
    const char *ntop_result = nullptr;
    if (addr.in6.sin6_family == AF_INET6) {
        ntop_result = inet_ntop(AF_INET6, &addr.in6.sin6_addr, buf, sizeof(buf));
    } else if (addr.in4.sin_family == AF_INET) {
        ntop_result = inet_ntop(AF_INET, &addr.in4.sin_addr, buf, sizeof(buf));
    }
    if (!ntop_result) {
        return "invalid address";
    }
    char buf2[50];
    snprintf(buf2, sizeof(buf2), ":%d, stream id %d", port(), id);
    std::string result(buf);
    result.append(buf2);
    return result;
}