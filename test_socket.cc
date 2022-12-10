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
    struct homa_recvmsg_control control;
    control.buffers[0] = 0;
    control.buffers[1] = 1;
    control.buffers[2] = 2;
    control.num_buffers = 3;
    sock.saveBuffers(&control);
    EXPECT_STREQ("0 1 2", getSaved(&sock).c_str());

    // Should be idempotent.
    sock.saveBuffers(&control);
    EXPECT_STREQ("0 1 2", getSaved(&sock).c_str());
}

TEST_F(TestSocket, getSavedBuffers) {
    struct homa_recvmsg_control control;
    for (uint32_t i = 0; i < 20; i++) {
        control.buffers[0] = 2*i + 1;
        control.num_buffers = 1;
        sock.saveBuffers(&control);
    }
    EXPECT_EQ(20U, sock.savedBuffers.size());

    // First attempt: limited by space in control.
    control.num_buffers = 0;
    sock.getSavedBuffers(&control);
    EXPECT_EQ(static_cast<uint32_t>(HOMA_MAX_BPAGES), control.num_buffers);
    EXPECT_EQ(1U, control.buffers[0]);
    EXPECT_EQ(3U, control.buffers[1]);
    EXPECT_EQ(static_cast<uint32_t>(2*HOMA_MAX_BPAGES - 1),
            control.buffers[HOMA_MAX_BPAGES-1]);

    // Second attempt: limited by available buffers.
    control.num_buffers = 0;
    sock.getSavedBuffers(&control);
    EXPECT_EQ(static_cast<uint32_t>(20 - HOMA_MAX_BPAGES), control.num_buffers);
    EXPECT_EQ(static_cast<uint32_t>(2*HOMA_MAX_BPAGES + 1), control.buffers[0]);
    EXPECT_EQ(static_cast<uint32_t>(2*HOMA_MAX_BPAGES + 3), control.buffers[1]);

    // Third attempt: no available buffers.
    control.num_buffers = 0;
    sock.getSavedBuffers(&control);
    EXPECT_EQ(0U, control.num_buffers);
}
TEST_F(TestSocket, getSavedBuffers_controlAlreadyHasSomeBuffers) {
    struct homa_recvmsg_control control;
    sock.savedBuffers.push_back(10);
    sock.savedBuffers.push_back(11);
    control.num_buffers = 3;
    sock.getSavedBuffers(&control);
    EXPECT_EQ(5U, control.num_buffers);
    EXPECT_EQ(10U, control.buffers[3]);
    EXPECT_EQ(11U, control.buffers[4]);
}