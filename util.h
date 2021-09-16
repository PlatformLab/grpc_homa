#ifndef UTIL_H
#define UTIL_H

#include <grpc/grpc.h>
#include <grpcpp/grpcpp.h>

#include "src/core/lib/surface/channel.h"

// This file contains miscellaneous small facilities that are useful
// in the Homa gRPC driver.

template<class P, class M>
size_t offsetOf(const M P::*member)
{
    return reinterpret_cast<size_t>(&( reinterpret_cast<P*>(0)->*member));
}

template<class P, class M>
P* containerOf(M* ptr, const M P::*member)
{
    return reinterpret_cast<P*>(reinterpret_cast<char*>(ptr)
            - offsetOf(member));
}

extern void          fillData(void *data, int length, int firstValue);
extern void          logMetadata(const grpc_metadata_batch* mdBatch,
                        const char *info);
extern std::string   stringForStatus(grpc::Status *status);
extern std::string   stringPrintf(const char* format, ...)
                        __attribute__((format(printf, 1, 2)));
extern const char *  symbolForCode(grpc::StatusCode code);

#endif // UTIL_H