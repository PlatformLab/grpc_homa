#define __UNIT_TEST__ 1
#include "homa_listener.h"
#include "mock.h"

// This file contains unit tests for homa_listener.cc and homa_listener.h.

class TestListener : public ::testing::Test {
public:
    grpc_core::Arena *arena;
    HomaListener *lis;
    HomaListener::Transport *trans;
    std::vector<HomaStream *> streams;
    grpc_stream_refcount refcount;
    HomaIncoming msg;
    uint32_t msgStreamId;
    grpc_closure closure1;

    static void closureFunc1(void* arg, grpc_error_handle error) {
        int64_t value = reinterpret_cast<int64_t>(arg);
        if (error != GRPC_ERROR_NONE) {
            Mock::logPrintf("; ", "closure1 invoked with %ld, error %s",
                    value, grpc_error_string(error));
        } else {
            Mock::logPrintf("; ", "closure1 invoked with %ld", value);
        }
    }

    TestListener()
        : arena(grpc_core::Arena::Create(2000))
        , lis(nullptr)
        , trans(nullptr)
        , streams()
        , refcount()
        , msg(2, true, 100, 0, 0, true, true)
    {
        int port = 4000;
        gpr_once_init(&HomaListener::shared_once, HomaListener::InitShared);
        Mock::setUp();
        lis = new HomaListener(nullptr, &port, false);
        trans = lis->transport;
        trans->accept_stream_cb = acceptStreamCallback;
        trans->accept_stream_data = this;
        GRPC_CLOSURE_INIT(&closure1, closureFunc1,
                reinterpret_cast<void *>(123), dummy);
    }

    ~TestListener()
    {
        grpc_core::ExecCtx exec_ctx;
        for (HomaStream *stream: streams) {
            delete stream;
        }
        delete lis;
        arena->Destroy();
    }

    static void acceptStreamCallback(void* fixture, grpc_transport* transport,
            const void* initInfo)
    {
        TestListener *test = reinterpret_cast<TestListener *>(fixture);
        HomaListener::Transport::StreamInit *init =
                static_cast<HomaListener::Transport::StreamInit *>(
                const_cast<void*>(initInfo));
        init->stream = new HomaStream(*init->streamId, test->trans->fd,
                &test->refcount, test->arena);
        test->streams.push_back(init->stream);
    }
};

TEST_F(TestListener, getStream_basics) {
    std::optional<grpc_core::MutexLock> lockGuard;

    // Id 100: add new stream
    msg.streamId.id = 100;
    HomaStream *stream1 = lis->transport->getStream(&msg, lockGuard);
    EXPECT_EQ(1U, trans->activeRpcs.size());
    EXPECT_EQ(100U, stream1->streamId.id);
    lockGuard.reset();

    // Id 200: add new stream
    msg.streamId.id = 200;
    HomaStream *stream2 = trans->getStream(&msg, lockGuard);
    EXPECT_EQ(2U, trans->activeRpcs.size());
    EXPECT_EQ(200U, stream2->streamId.id);
    lockGuard.reset();

    // Id 100 again
    msg.streamId.id = 100;
    HomaStream *stream3 = trans->getStream(&msg, lockGuard);
    EXPECT_EQ(2U, trans->activeRpcs.size());
    EXPECT_EQ(100U, stream3->streamId.id);
    EXPECT_EQ(stream1, stream3);
}
TEST_F(TestListener, getStream_noCallback) {
    std::optional<grpc_core::MutexLock> lockGuard;
    trans->accept_stream_cb = nullptr;
    HomaStream *stream1 = trans->getStream(&msg, lockGuard);
    EXPECT_EQ(0U, trans->activeRpcs.size());
    EXPECT_EQ(nullptr, stream1);
}

TEST_F(TestListener, destroy_stream) {
    HomaStream *stream;
    grpc_core::ExecCtx execCtx;
    {
        std::optional<grpc_core::MutexLock> lockGuard;
        msg.streamId.id = 100;
        stream = trans->getStream(&msg, lockGuard);
        EXPECT_EQ(1U, trans->activeRpcs.size());
        EXPECT_EQ(100U, stream->streamId.id);
        ASSERT_EQ(1U, streams.size());
        ASSERT_EQ(stream, streams[0]);
    }

    HomaListener::Transport::destroy_stream(&trans->vtable,
            reinterpret_cast <grpc_stream*>(stream), &closure1);
    free(stream);
    streams.clear();
    execCtx.Flush();
    EXPECT_EQ(0U, trans->activeRpcs.size());
    EXPECT_STREQ("closure1 invoked with 123", Mock::log.c_str());
}