#define __UNIT_TEST__ 1
#include "homa_listener.h"
#include "mock.h"

#include "src/core/lib/resource_quota/resource_quota.h"

// This file contains unit tests for homa_listener.cc and homa_listener.h.

class TestListener : public ::testing::Test {
public:
    HomaListener *lis;
    HomaListener::Transport *trans;
    std::vector<HomaStream *> streams;
    grpc_stream_refcount refcount;
    HomaSocket sock;
    HomaIncoming msg;
    uint32_t msgStreamId;
    grpc_closure closure1;

    static void closureFunc1(void* arg, grpc_error_handle error) {
        int64_t value = reinterpret_cast<int64_t>(arg);
        if (!error.ok()) {
            Mock::logPrintf("; ", "closure1 invoked with %ld, error %s",
                    value, error.ToString().c_str());
        } else {
            Mock::logPrintf("; ", "closure1 invoked with %ld", value);
        }
    }

    TestListener()
        : lis(nullptr)
        , trans(nullptr)
        , streams()
        , refcount()
        , sock(Mock::bufRegion)
        , msg(&sock, 2, true, 100, 0, true, true)
        , msgStreamId()
        , closure1()
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
            stream->~HomaStream();
            free(stream);
        }
        delete lis;
    }

    static void acceptStreamCallback(void* fixture, grpc_transport* transport,
            const void* initInfo)
    {
        TestListener *test = reinterpret_cast<TestListener *>(fixture);
        HomaListener::Transport::StreamInit *init =
                static_cast<HomaListener::Transport::StreamInit *>(
                const_cast<void*>(initInfo));

        // This contortion is needed to be consistent with other code that
        // creates HomaStreams with placement new.
        init->stream = reinterpret_cast<HomaStream *>(malloc(sizeof(HomaStream)));
        new (init->stream) HomaStream(false, *init->streamId,
                test->trans->sock.getFd(), &test->refcount);
        test->streams.push_back(init->stream);
    }
};

TEST_F(TestListener, getStream_basics) {
    std::optional<grpc_core::MutexLock> lockGuard;
    StreamId streamId(100);

    // Id 100: add new stream
    HomaStream *stream1 = lis->transport->getStream(&streamId, lockGuard, true);
    EXPECT_EQ(1U, trans->activeRpcs.size());
    EXPECT_EQ(100U, stream1->streamId.id);
    lockGuard.reset();

    // Id 200: add new stream
    streamId.id = 200;
    HomaStream *stream2 = trans->getStream(&streamId, lockGuard, true);
    EXPECT_EQ(2U, trans->activeRpcs.size());
    EXPECT_EQ(200U, stream2->streamId.id);
    lockGuard.reset();

    // Id 100 again
    streamId.id = 100;
    HomaStream *stream3 = trans->getStream(&streamId, lockGuard, true);
    EXPECT_EQ(2U, trans->activeRpcs.size());
    EXPECT_EQ(100U, stream3->streamId.id);
    EXPECT_EQ(stream1, stream3);
}
TEST_F(TestListener, getStream_dontCreate) {
    std::optional<grpc_core::MutexLock> lockGuard;
    StreamId streamId(100);

    // Id 100: add new stream
    HomaStream *stream = lis->transport->getStream(&streamId, lockGuard, false);
    EXPECT_EQ(nullptr, stream);
    EXPECT_EQ(0U, trans->activeRpcs.size());
}
TEST_F(TestListener, getStream_noCallback) {
    std::optional<grpc_core::MutexLock> lockGuard;
    StreamId streamId(100);
    trans->accept_stream_cb = nullptr;
    HomaStream *stream1 = trans->getStream(&streamId, lockGuard, true);
    EXPECT_EQ(0U, trans->activeRpcs.size());
    EXPECT_EQ(nullptr, stream1);
}

TEST_F(TestListener, destroy_stream) {
    HomaStream *stream;
    grpc_core::ExecCtx execCtx;
    StreamId streamId(100);
    {
        std::optional<grpc_core::MutexLock> lockGuard;
        stream = trans->getStream(&streamId, lockGuard, true);
        EXPECT_EQ(1U, trans->activeRpcs.size());
        EXPECT_EQ(100U, streamId.id);
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