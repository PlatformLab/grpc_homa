#include "rpc_id.h"

/**
 * Construct an RpcId from a gRPC address.
 * \param gaddr
 *      The address of the peer for the RPC.
 * \param id
 *      Unique id assigned to this RPC by the client.
 */
RpcId::RpcId(grpc_resolved_address *gaddr, uint32_t id)
    : addr()
    , addr_size(gaddr->len)
    , id(id)
{
    // Homa currently understands only ipv4 addresses.
    GPR_ASSERT(reinterpret_cast<struct sockaddr*>(gaddr)->sa_family == AF_INET);
    GPR_ASSERT(addr_size <= sizeof(addr));
    
    bcopy(addr, gaddr->addr, addr_size);
}

/**
 * Comparison operator for RpcIds.
 * \param other
 *      RpcID to compare against
 * \return
 *      True means the RpcIds match, false means they don't.
 */
bool RpcId::operator==(const RpcId& other) const
{
    return (id == other.id) && (addr_size == other.addr_size)
            && (bcmp(addr, other.addr, addr_size) == 0);
}