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
    , addrSize(gaddr->len)
    , id(id)
{
    // Homa currently understands only ipv4 addresses.
    GPR_ASSERT(reinterpret_cast<struct sockaddr*>(gaddr)->sa_family == AF_INET);
    GPR_ASSERT(addrSize <= sizeof(addr));
    
    bcopy(gaddr->addr, addr, addrSize);
}

/**
 * Constructor for testing.
 * \param id
 *      Identifier for this particular RPC.
 */
StreamId::StreamId(uint32_t id)
    : addr()
    , addrSize()
    , id(id)
{
    inaddr()->sin_family = AF_INET;
    inaddr()->sin_addr.s_addr = 0x01020304;
    inaddr()->sin_port = htons(40);
    addrSize = sizeof(struct sockaddr_in);
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
    return (id == other.id) && (addrSize == other.addrSize)
            && (bcmp(addr, other.addr, addrSize) == 0);
}