#include <string>

#include "homa_socket.h"
#include "mock.h"
#include "util.h"

// This file contains unit tests for homa_socket.cc and homa_socket.h.

class TestSocket : public ::testing::Test {
public:
    HomaSocket sock;

    TestSocket()
        : sock(nullptr)
    {
        Mock::setUp();
    }

    ~TestSocket()
    {
    }

    /**
     * Return a human-readable string describing all of the saved buffer
     * info in a HomaSocket.
     * \param sock
     *      Socket whose saved buffers should be scanned.
     */
    std::string getSaved(HomaSocket *sock)
    {
        std::string result;
        for (uint32_t offset: sock->savedBuffers) {
            char buffer[100];
            if (!result.empty()) {
                result.append(" ");
            }
            snprintf(buffer, sizeof(buffer), "%d", offset);
            result.append(buffer);
        }
        return result;
    }
};

TEST_F(TestSocket, saveBuffers) {
    struct homa_recvmsg_args recvArgs;
    recvArgs.bpage_offsets[0] = 0;
    recvArgs.bpage_offsets[1] = 1;
    recvArgs.bpage_offsets[2] = 2;
    recvArgs.num_bpages = 3;
    sock.saveBuffers(&recvArgs);
    EXPECT_STREQ("0 1 2", getSaved(&sock).c_str());

    // Should be idempotent.
    sock.saveBuffers(&recvArgs);
    EXPECT_STREQ("0 1 2", getSaved(&sock).c_str());
}

TEST_F(TestSocket, getSavedBuffers) {
    struct homa_recvmsg_args recvArgs;
    for (uint32_t i = 0; i < 20; i++) {
        recvArgs.bpage_offsets[0] = 2*i + 1;
        recvArgs.num_bpages = 1;
        sock.saveBuffers(&recvArgs);
    }
    EXPECT_EQ(20U, sock.savedBuffers.size());

    // First attempt: limited by space in control.
    recvArgs.num_bpages = 0;
    sock.getSavedBuffers(&recvArgs);
    EXPECT_EQ(static_cast<uint32_t>(HOMA_MAX_BPAGES), recvArgs.num_bpages);
    EXPECT_EQ(1U, recvArgs.bpage_offsets[0]);
    EXPECT_EQ(3U, recvArgs.bpage_offsets[1]);
    EXPECT_EQ(static_cast<uint32_t>(2*HOMA_MAX_BPAGES - 1),
            recvArgs.bpage_offsets[HOMA_MAX_BPAGES-1]);

    // Second attempt: limited by available buffers.
    recvArgs.num_bpages = 0;
    sock.getSavedBuffers(&recvArgs);
    EXPECT_EQ(static_cast<uint32_t>(20 - HOMA_MAX_BPAGES), recvArgs.num_bpages);
    EXPECT_EQ(static_cast<uint32_t>(2*HOMA_MAX_BPAGES + 1), recvArgs.bpage_offsets[0]);
    EXPECT_EQ(static_cast<uint32_t>(2*HOMA_MAX_BPAGES + 3), recvArgs.bpage_offsets[1]);

    // Third attempt: no available buffers.
    recvArgs.num_bpages = 0;
    sock.getSavedBuffers(&recvArgs);
    EXPECT_EQ(0U, recvArgs.num_bpages);
}
TEST_F(TestSocket, getSavedBuffers_argsAlreadyHasSomeBuffers) {
    struct homa_recvmsg_args control;
    sock.savedBuffers.push_back(10);
    sock.savedBuffers.push_back(11);
    control.num_bpages = 3;
    sock.getSavedBuffers(&control);
    EXPECT_EQ(5U, control.num_bpages);
    EXPECT_EQ(10U, control.bpage_offsets[3]);
    EXPECT_EQ(11U, control.bpage_offsets[4]);
}