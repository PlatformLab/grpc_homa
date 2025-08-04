#ifndef MOCK_H
#define MOCK_H

#include <deque>
#include <string>

#include "grpc/impl/codegen/log.h"

#include "gtest/gtest.h"

#include "homa.h"

#include "wire.h"

/* This class defines additional variables and functions that can be used
 * by unit tests as part of mocking out system features.
 */
class Mock {
public:
    // Used by some methods as the errno to return after a simulated failure.
    static int errorCode;

    // The variables below can be set to non-zero values by unit tests in order
    // to simulate error returns from various functions. If bit 0 is set to 1,
    // the next call to the function will fail; bit 1 corresponds to the next
    // call after that, and so on.
    static int recvmsgErrors;
    static int sendmsgErrors;

    static int buffersReturned;

    // Holds all messages sent by sendmsg.
    static std::deque<std::vector<uint8_t>> homaMessages;

    // Return info for upcoming invocations of homa_recv.
    static std::deque<Wire::Header> recvmsgHeaders;
    static std::deque<ssize_t> recvmsgLengths;
    static std::deque<ssize_t> recvmsgReturns;

    // Accumulates various information over the course of a test, which
    // can then be queried.
    static std::string log;

    // Buffer region for the Homa socket.
    static uint8_t *bufRegion;

    static int        checkError(int *errorMask);
    static grpc_core::Slice
                      dataSlice(size_t length, int firstValue);
    static void       gprLog(gpr_log_func_args* args);
    static void       logData(const char *separator, const void *data,
                        int length);
    static void       logMetadata(const char *separator,
                        const grpc_metadata_batch *batch);
    static void       logPrintf(const char *separator, const char* format, ...);
    static void       logSliceBuffer(const char *separator,
                        const grpc_core::SliceBuffer *sliceBuffer);
    static void       metadataBatchAppend(grpc_metadata_batch* batch,
                        const char *key, const char *value);
    static void       setUp(void);
    static ::testing::AssertionResult
                        substr(const std::string& s,
                        const std::string& substring);
};

#define EXPECT_SUBSTR(sub, str) EXPECT_TRUE(Mock::substr((str), (sub)))
#define EXPECT_NSUBSTR(sub, str) EXPECT_FALSE(Mock::substr((str), (sub)))

#endif // MOCK_H
