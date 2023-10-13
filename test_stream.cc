#include "homa_stream.h"
#include "mock.h"
#include "util.h"

#include "src/core/lib/resource_quota/resource_quota.h"

// This file contains unit tests for homa_stream.cc and homa_stream.h.

struct ArenaDeleter {
    void operator()(grpc_core::Arena* arena)
    {
        arena->Destroy();
    }
};

class TestStream : public ::testing::Test {
public:
    uint8_t bufRegion[5000];
    HomaSocket sock;
    grpc_core::MemoryAllocator allocator;
    std::unique_ptr<grpc_core::Arena, ArenaDeleter> arena;
    grpc_stream_refcount refcount;
    StreamId streamId;
    HomaStream stream;
    grpc_closure closure1;
    grpc_closure closure2;
    grpc_metadata_batch batch;
    grpc_metadata_batch batch2;
    grpc_transport_stream_op_batch op;
    grpc_transport_stream_op_batch_payload payload;

    static void closureFunc1(void* arg, grpc_error_handle error) {
        int64_t value = reinterpret_cast<int64_t>(arg);
        if (!error.ok()) {
            Mock::logPrintf("; ", "closure1 invoked with %ld, error %s",
                    value, error.ToString().c_str());
        } else {
            Mock::logPrintf("; ", "closure1 invoked with %ld", value);
        }
    }

    static void closureFunc2(void* arg, grpc_error_handle error) {
        int64_t value = reinterpret_cast<int64_t>(arg);
        if (!error.ok()) {
            Mock::logPrintf("; ", "closure2 invoked with %ld, error %s",
                    value, error.ToString().c_str());
        } else {
            Mock::logPrintf("; ", "closure2 invoked with %ld", value);
        }
    }

    TestStream()
        : bufRegion()
        , sock(bufRegion)
        , allocator(grpc_core::ResourceQuota::Default()->memory_quota()->
                CreateMemoryAllocator("test"))
        , arena(grpc_core::Arena::Create(2000, &allocator))
        , refcount()
        , streamId(33)
        , stream(false, streamId, 3, &refcount)
        , closure1()
        , closure2()
        , batch(arena.get())
        , batch2(arena.get())
        , op()
        , payload(nullptr)
    {
        Mock::setUp();
        GRPC_CLOSURE_INIT(&closure1, closureFunc1,
                reinterpret_cast<void *>(123), dummy);
        GRPC_CLOSURE_INIT(&closure2, closureFunc2,
                reinterpret_cast<void *>(456), dummy);
        op.payload = &payload;
    }

    ~TestStream()
    {
        batch.Clear();
        batch2.Clear();
    }
};

TEST_F(TestStream, flush_noMessage) {
    stream.flush();
    EXPECT_STREQ("", Mock::log.c_str());
}
TEST_F(TestStream, flush_sendRequest) {
    stream.hdr()->flags |= Wire::Header::initMdPresent;
    stream.flush();
    EXPECT_SUBSTR("homa_sendv: 1 iovecs", Mock::log.c_str());
    EXPECT_EQ(123U, stream.sentHomaId);
}
TEST_F(TestStream, flush_sendReply) {
    stream.hdr()->flags |= Wire::Header::trailMdPresent;
    stream.homaRequestId = 999;
    stream.flush();
    EXPECT_SUBSTR("homa_replyv: 1 iovecs", Mock::log.c_str());
    EXPECT_EQ(0U, stream.homaRequestId);
}
TEST_F(TestStream, flush_error) {
    stream.hdr()->flags |= Wire::Header::trailMdPresent;
    stream.homaRequestId = 999;
    Mock::homaReplyvErrors = 1;
    stream.flush();
    EXPECT_SUBSTR("gpr_log: Couldn't send Homa response: Input/output error",
            Mock::log.c_str());
}

TEST_F(TestStream, saveCallbacks_basics) {
    grpc_core::ExecCtx execCtx;
    op.recv_initial_metadata = true;
    op.payload->recv_initial_metadata.recv_initial_metadata = &batch;
    op.payload->recv_initial_metadata.recv_initial_metadata_ready = &closure1;

    op.recv_message = true;
    absl::optional<grpc_core::SliceBuffer> message;
    op.payload->recv_message.recv_message = &message;
    grpc_closure closure3;
    GRPC_CLOSURE_INIT(&closure3, closureFunc1,
            reinterpret_cast<void *>(777), dummy);
    op.payload->recv_message.recv_message_ready = &closure3;

    op.recv_trailing_metadata = true;
    op.payload->recv_trailing_metadata.recv_trailing_metadata = &batch;
    op.payload->recv_trailing_metadata.recv_trailing_metadata_ready = &closure2;

    stream.saveCallbacks(&op);
    execCtx.Flush();
    EXPECT_NE(nullptr, stream.initMdClosure);
    EXPECT_NE(nullptr, stream.messageClosure);
    EXPECT_NE(nullptr, stream.trailMdClosure);
    EXPECT_STREQ("", Mock::log.c_str());
}
TEST_F(TestStream, saveCallbacks_notifyError) {
    grpc_core::ExecCtx execCtx;
    op.recv_initial_metadata = true;
    op.payload->recv_initial_metadata.recv_initial_metadata = &batch;
    op.payload->recv_initial_metadata.recv_initial_metadata_ready = &closure1;

    op.recv_message = true;
    absl::optional<grpc_core::SliceBuffer> message;
    op.payload->recv_message.recv_message = &message;
    grpc_closure closure3;
    GRPC_CLOSURE_INIT(&closure3, closureFunc1,
            reinterpret_cast<void *>(777), dummy);
    op.payload->recv_message.recv_message_ready = &closure3;

    op.recv_trailing_metadata = true;
    op.payload->recv_trailing_metadata.recv_trailing_metadata = &batch;
    op.payload->recv_trailing_metadata.recv_trailing_metadata_ready = &closure2;

    stream.error = GRPC_ERROR_CREATE("test error");
    stream.saveCallbacks(&op);
    execCtx.Flush();
    EXPECT_EQ(nullptr, stream.initMdClosure);
    EXPECT_EQ(nullptr, stream.messageClosure);
    EXPECT_EQ(nullptr, stream.trailMdClosure);
    EXPECT_SUBSTR("closure1 invoked with 123, error", Mock::log.c_str());
    EXPECT_SUBSTR("closure1 invoked with 777, error", Mock::log.c_str());
    EXPECT_SUBSTR("closure2 invoked with 456, error", Mock::log.c_str());
    EXPECT_SUBSTR("test error", Mock::log.c_str());
}

TEST_F(TestStream, metadataLength) {
    Mock::metadataBatchAppend(&batch, "key1", "7 chars");
    Mock::metadataBatchAppend(&batch, "k2", "0123456789");
    EXPECT_EQ(2*sizeof(Wire::Mdata) + 23, HomaStream::metadataLength(&batch));
    EXPECT_STREQ("", Mock::log.c_str());
}

TEST_F(TestStream, serializeMetadata_basics) {
    size_t initialSize = stream.xmitSize;
    stream.serializeMetadata("key1", 4, "7 chars", 7);
    size_t length = stream.xmitSize - initialSize;
    EXPECT_EQ(sizeof(Wire::Mdata) + 11, length);
    Wire::dumpMetadata(stream.xmitBuffer + initialSize, length,
            GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("gpr_log: Key: key1, value: 7 chars", Mock::log.c_str());
}
TEST_F(TestStream, serializeMetadata_chunkOverflow) {
    // First metadata item fits in current chunk.
    size_t initialSize = stream.xmitSize;
    stream.lastVecAvail = 30;
    stream.serializeMetadata("key1", 4, "7 chars", 7);
    EXPECT_EQ(1U, stream.vecs.size());
    size_t length = sizeof(Wire::Mdata) + 11;
    EXPECT_EQ(length + initialSize, stream.vecs[0].iov_len);

    // Second metadata item requires additional chunk.
    stream.serializeMetadata("k2", 2, "0123456789", 10);
    EXPECT_EQ(2U, stream.vecs.size());
    EXPECT_EQ(sizeof(Wire::Mdata) + 12, stream.vecs[1].iov_len);

    Wire::dumpMetadata(stream.xmitBuffer + initialSize, length,
            GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("gpr_log: Key: key1, value: 7 chars",
            Mock::log.c_str());
    Mock::log.clear();
    Wire::dumpMetadata(stream.vecs[1].iov_base, stream.vecs[1].iov_len,
            GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("gpr_log: Key: k2, value: 0123456789",
            Mock::log.c_str());
}

TEST_F(TestStream, serializeMetadataBatch) {
    Mock::metadataBatchAppend(&batch, "key1", "7 chars");
    Mock::metadataBatchAppend(&batch, "k2", "0123456789");
    size_t initialSize = stream.xmitSize;
    stream.serializeMetadataBatch(&batch);
    size_t length = stream.xmitSize - initialSize;
    EXPECT_EQ(2*sizeof(Wire::Mdata) + 23, length);
    Wire::dumpMetadata(stream.xmitBuffer + initialSize, length,
            GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("gpr_log: Key: key1, value: 7 chars; "
            "gpr_log: Key: k2, value: 0123456789",
            Mock::log.c_str());
}

TEST_F(TestStream, xmit_initialMetadata) {
    op.send_initial_metadata = true;
    payload.send_initial_metadata.send_initial_metadata = &batch;
    Mock::metadataBatchAppend(&batch, "key1", "7 chars");
    Mock::metadataBatchAppend(&batch, "k2", "0123456789");

    // First xmit sends nothing (waiting for more info.
    stream.xmit(&op);
    EXPECT_EQ(0U, Mock::homaMessages.size());

    // Second xmit of metadata flushes first batch, but not second.
    payload.send_initial_metadata.send_initial_metadata = &batch2;
    Mock::metadataBatchAppend(&batch2, "key3", "key3 value");
    stream.xmit(&op);
    ASSERT_EQ(1U, Mock::homaMessages.size());
    Wire::dumpHeader(Mock::homaMessages[0].data(), GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("homa_sendv: 1 iovecs, 60 bytes; "
            "gpr_log: Header: id: 33, sequence 1, initMdBytes 39, "
            "initMdPresent, request",
            Mock::log.c_str());
    Mock::log.clear();
    Wire::dumpMetadata(Mock::homaMessages[0].data() + sizeof(Wire::Header),
            Mock::homaMessages[0].size() - sizeof(Wire::Header),
            GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("gpr_log: Key: key1, value: 7 chars; "
            "gpr_log: Key: k2, value: 0123456789",
            Mock::log.c_str());

    // Send message to flush the second metadata batch.
    Mock::log.clear();
    op.send_initial_metadata = false;
    op.send_message = true;
    grpc_core::SliceBuffer *slices = new grpc_core::SliceBuffer;
    slices->Append(Mock::dataSlice(100, 1000));
    payload.send_message.send_message = slices;
    stream.xmit(&op);

    ASSERT_EQ(2U, Mock::homaMessages.size());
    Wire::dumpHeader(Mock::homaMessages[1].data(), GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("homa_sendv: 2 iovecs, 143 bytes; "
            "gpr_log: Header: id: 33, sequence 2, initMdBytes 22, "
            "messageBytes 100, initMdPresent, messageComplete, request",
            Mock::log.c_str());
    Mock::log.clear();
    Wire::Header *hdr = reinterpret_cast<Wire::Header *>(
            Mock::homaMessages[1].data());
    Wire::dumpMetadata(Mock::homaMessages[1].data() + sizeof(Wire::Header),
            ntohl(hdr->initMdBytes), GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("gpr_log: Key: key3, value: key3 value",
            Mock::log.c_str());
    Mock::log.clear();
    Mock::logData("; ",
            Mock::homaMessages[1].data() + sizeof(Wire::Header)
            + ntohl(hdr->initMdBytes), ntohl(hdr->messageBytes));
    EXPECT_STREQ("1000-1099",
            Mock::log.c_str());

}
TEST_F(TestStream, xmit_initialMetadataTooLarge) {
    op.send_initial_metadata = true;
    payload.send_initial_metadata.send_initial_metadata = &batch;
    Mock::metadataBatchAppend(&batch, "key1", "7 chars");
    Mock::metadataBatchAppend(&batch, "k2", "0123456789");
    stream.maxMessageLength = 50;
    stream.xmit(&op);
    EXPECT_EQ(0U, Mock::homaMessages.size());
    EXPECT_SUBSTR("Too much initial metadata",
            Mock::log.c_str());
    EXPECT_SUBSTR("Too much initial metadata",
            stream.error.ToString().c_str());

}
TEST_F(TestStream, xmit_trailingMetadataTooLarge) {
    op.send_trailing_metadata = true;
    payload.send_trailing_metadata.send_trailing_metadata = &batch;
    Mock::metadataBatchAppend(&batch, "key1", "7 chars");
    Mock::metadataBatchAppend(&batch, "k2", "0123456789");
    stream.maxMessageLength = 50;
    stream.xmit(&op);
    EXPECT_EQ(0U, Mock::homaMessages.size());
    EXPECT_SUBSTR("Too much trailing metadata",
            Mock::log.c_str());
    EXPECT_SUBSTR("Too much trailing metadata",
            stream.error.ToString().c_str());

}
TEST_F(TestStream, xmit_onlyTrailingMetadata) {
    op.send_trailing_metadata = true;
    payload.send_trailing_metadata.send_trailing_metadata = &batch;
    Mock::metadataBatchAppend(&batch, "key1", "7 chars");
    Mock::metadataBatchAppend(&batch, "k2", "0123456789");
    stream.homaRequestId = 999;
    stream.xmit(&op);
    ASSERT_EQ(1U, Mock::homaMessages.size());
    Wire::dumpHeader(Mock::homaMessages[0].data(), GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("homa_replyv: 1 iovecs, 60 bytes; "
            "gpr_log: Header: id: 33, sequence 1, trailMdBytes 39, "
            "trailMdPresent",
            Mock::log.c_str());
    Mock::log.clear();
    Wire::dumpMetadata(Mock::homaMessages[0].data() + sizeof(Wire::Header),
            Mock::homaMessages[0].size() - sizeof(Wire::Header),
            GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("gpr_log: Key: key1, value: 7 chars; "
            "gpr_log: Key: k2, value: 0123456789",
            Mock::log.c_str());
}
TEST_F(TestStream, xmit_onlyMessageData) {
    op.send_message = true;
    grpc_core::SliceBuffer *slices = new grpc_core::SliceBuffer;
    slices->Append(Mock::dataSlice(100, 1000));
    slices->Append(Mock::dataSlice(200, 2000));
    slices->Append(Mock::dataSlice(300, 3000));
    payload.send_message.send_message = slices;
    stream.xmit(&op);
    ASSERT_EQ(1U, Mock::homaMessages.size());
    Wire::dumpHeader(Mock::homaMessages[0].data(), GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("homa_sendv: 4 iovecs, 621 bytes; "
            "gpr_log: Header: id: 33, sequence 1, messageBytes 600, "
            "messageComplete, request",
            Mock::log.c_str());
    Mock::log.clear();
    Mock::logData("; ",
            Mock::homaMessages[0].data() + sizeof(Wire::Header),
            Mock::homaMessages[0].size() - sizeof(Wire::Header));
    EXPECT_STREQ("1000-1099 2000-2199 3000-3299",
            Mock::log.c_str());
}
TEST_F(TestStream, xmit_everything) {
    op.send_initial_metadata = true;
    payload.send_initial_metadata.send_initial_metadata = &batch;
    Mock::metadataBatchAppend(&batch, "key1", "7 chars");
    stream.homaRequestId = 999;

    op.send_message = true;
    grpc_core::SliceBuffer *slices = new grpc_core::SliceBuffer;
    slices->Append(Mock::dataSlice(100, 1000));
    slices->Append(Mock::dataSlice(200, 2000));
    slices->Append(Mock::dataSlice(300, 3000));
    payload.send_message.send_message = slices;

    op.send_trailing_metadata = true;
    payload.send_trailing_metadata.send_trailing_metadata = &batch2;
    Mock::metadataBatchAppend(&batch2, "k2", "0123456789");

    stream.xmit(&op);
    ASSERT_EQ(1U, Mock::homaMessages.size());
    Wire::dumpHeader(Mock::homaMessages[0].data(), GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("homa_replyv: 4 iovecs, 660 bytes; "
            "gpr_log: Header: id: 33, sequence 1, initMdBytes 19, "
            "messageBytes 600, trailMdBytes 20, initMdPresent, "
            "messageComplete, trailMdPresent",
            Mock::log.c_str());
    Mock::log.clear();
    Wire::dumpMetadata(Mock::homaMessages[0].data() + sizeof(Wire::Header),
            19, GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("gpr_log: Key: key1, value: 7 chars",
            Mock::log.c_str());
    Mock::log.clear();
    Wire::dumpMetadata(Mock::homaMessages[0].data()
            + sizeof(Wire::Header) + 19,
            20, GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("gpr_log: Key: k2, value: 0123456789",
            Mock::log.c_str());
    Mock::log.clear();
    Mock::logData("; ",
            Mock::homaMessages[0].data() + sizeof(Wire::Header) + 39,
            Mock::homaMessages[0].size() - (sizeof(Wire::Header) + 39));
    EXPECT_STREQ("1000-1099 2000-2199 3000-3299",
            Mock::log.c_str());
}
TEST_F(TestStream, xmit_multipleHomaMessages) {
    op.send_initial_metadata = true;
    payload.send_initial_metadata.send_initial_metadata = &batch;
    Mock::metadataBatchAppend(&batch, "key10", "7 chars");
    stream.homaRequestId = 999;

    op.send_message = true;
    grpc_core::SliceBuffer *slices = new grpc_core::SliceBuffer;
    slices->Append(Mock::dataSlice(100, 1000));
    slices->Append(Mock::dataSlice(500, 2000));
    slices->Append(Mock::dataSlice(100, 3000));
    payload.send_message.send_message = slices;

    op.send_trailing_metadata = true;
    payload.send_trailing_metadata.send_trailing_metadata = &batch2;
    Mock::metadataBatchAppend(&batch2, "k2", "0123456789a");

    stream.maxMessageLength = 301;
    stream.xmit(&op);
    ASSERT_EQ(3U, Mock::homaMessages.size());
    EXPECT_STREQ("homa_replyv: 3 iovecs, 301 bytes; "
            "homa_sendv: 2 iovecs, 301 bytes; "
            "homa_sendv: 3 iovecs, 202 bytes",
            Mock::log.c_str());

    // First Homa message: initial metadata plus some message data.
    Mock::log.clear();
    Wire::dumpHeader(Mock::homaMessages[0].data(), GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("gpr_log: Header: id: 33, sequence 1, initMdBytes 20, "
            "messageBytes 260, initMdPresent",
            Mock::log.c_str());
    Mock::log.clear();
    Wire::dumpMetadata(Mock::homaMessages[0].data() + sizeof(Wire::Header),
            20, GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("gpr_log: Key: key10, value: 7 chars",
            Mock::log.c_str());
    Mock::log.clear();
    Mock::logData("; ",
            Mock::homaMessages[0].data() + sizeof(Wire::Header) + 20,
            Mock::homaMessages[0].size() - (sizeof(Wire::Header) + 20));
    EXPECT_STREQ("1000-1099 2000-2159", Mock::log.c_str());

    // Second Homa message: more message data.
    Mock::log.clear();
    Wire::dumpHeader(Mock::homaMessages[1].data(), GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("gpr_log: Header: id: 33, sequence 2, messageBytes 280, "
            "request", Mock::log.c_str());
    Mock::log.clear();
    Mock::logData("; ", Mock::homaMessages[1].data() + sizeof(Wire::Header),
            Mock::homaMessages[1].size() - (sizeof(Wire::Header)));
    EXPECT_STREQ("2160-2439", Mock::log.c_str());

    // Third Homa message; last message data plus trailing metadata.
    Mock::log.clear();
    Wire::dumpHeader(Mock::homaMessages[2].data(), GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("gpr_log: Header: id: 33, sequence 3, messageBytes 160, "
            "trailMdBytes 21, messageComplete, trailMdPresent, request",
            Mock::log.c_str());
    Mock::log.clear();
    Wire::dumpMetadata(Mock::homaMessages[2].data() + sizeof(Wire::Header),
            21, GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("gpr_log: Key: k2, value: 0123456789a",
            Mock::log.c_str());
    Mock::log.clear();
    Mock::logData("; ",
            Mock::homaMessages[2].data() + sizeof(Wire::Header) + 21,
            Mock::homaMessages[2].size() - (sizeof(Wire::Header) + 21));
    EXPECT_STREQ("2440-2499 3000-3099", Mock::log.c_str());
}
TEST_F(TestStream, xmit_emptyMessage) {
    op.send_message = true;
    payload.send_message.send_message = new grpc_core::SliceBuffer;
    stream.xmit(&op);
    ASSERT_EQ(1U, Mock::homaMessages.size());
    Wire::dumpHeader(Mock::homaMessages[0].data(), GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("homa_sendv: 1 iovecs, 21 bytes; gpr_log: Header: id: 33, "
            "sequence 1, messageComplete, request",
            Mock::log.c_str());
    Mock::log.clear();
    Mock::logData("; ",
            Mock::homaMessages[0].data() + sizeof(Wire::Header),
            Mock::homaMessages[0].size() - sizeof(Wire::Header));
    EXPECT_STREQ("empty block", Mock::log.c_str());
}
TEST_F(TestStream, xmit_setTrailMdSent) {
    // The flag should get set even if there is no metadata to send.
    op.send_trailing_metadata = true;
    payload.send_trailing_metadata.send_trailing_metadata = &batch;

    // These statements allow us to make sure that transfer doesn't
    // get called (because the stream is a client stream).
    stream.trailMdClosure = &closure1;
    stream.trailMd = &batch;
    stream.incoming.emplace_back(new HomaIncoming(&sock, 1, false,
            0, 0, false, true));
    stream.isServer = false;
    grpc_core::ExecCtx execCtx;
    stream.xmit(&op);
    execCtx.Flush();
    EXPECT_TRUE(stream.trailMdSent);
    EXPECT_EQ(1U, stream.incoming.size());
    EXPECT_NSUBSTR("closure", Mock::log.c_str());
}
TEST_F(TestStream, xmit_setTrailMdSentAndCallTransfer) {
    op.send_trailing_metadata = true;
    payload.send_trailing_metadata.send_trailing_metadata = &batch;
    stream.trailMdClosure = &closure1;
    stream.trailMd = &batch;
    stream.incoming.emplace_back(new HomaIncoming(&sock, 1, false,
            0, 0, false, true));
    stream.isServer = true;
    grpc_core::ExecCtx execCtx;
    stream.xmit(&op);
    execCtx.Flush();
    EXPECT_TRUE(stream.trailMdSent);
    EXPECT_EQ(0U, stream.incoming.size());
    EXPECT_SUBSTR("closure1 invoked with 123", Mock::log.c_str());
}

TEST_F(TestStream, sendDummyResponse) {
    stream.homaRequestId = 999;
    stream.sendDummyResponse();
    ASSERT_EQ(1U, Mock::homaMessages.size());
    Wire::dumpHeader(Mock::homaMessages[0].data(), GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("gpr_log: Header: id: 33, sequence 0, emptyResponse",
            Mock::log.c_str());
}
TEST_F(TestStream, sendDummyResponse_errorSendingResponse) {
    Mock::homaReplyErrors = 1;
    stream.homaRequestId = 999;
    stream.sendDummyResponse();
    EXPECT_SUBSTR("Couldn't send dummy Homa response", Mock::log.c_str());
    EXPECT_EQ(1U, Mock::homaMessages.size());
}

TEST_F(TestStream, transferData_outOfSequence) {
    stream.initMdClosure = &closure1;
    stream.initMd = &batch;
    HomaIncoming *msg = new HomaIncoming(&sock, 2, true, 0, 0,
            false, false);
    stream.incoming.emplace_back(msg);
    grpc_core::ExecCtx execCtx;
    stream.transferData();
    execCtx.Flush();
    EXPECT_STREQ("", Mock::log.c_str());
    EXPECT_EQ(1U, stream.incoming.size());
}
TEST_F(TestStream, transferData_initialMetadata) {
    bool trailMdAvail = false;
    stream.initMdClosure = &closure1;
    stream.initMd = &batch;
    stream.initMdTrailMdAvail = &trailMdAvail;
    stream.incoming.emplace_back(new HomaIncoming(&sock, 1, true,
            0, 0, false, false));
    grpc_core::ExecCtx execCtx;
    stream.transferData();
    execCtx.Flush();
    EXPECT_STREQ("closure1 invoked with 123", Mock::log.c_str());
    Mock::log.clear();
    Mock::logMetadata("; ", &batch);
    EXPECT_STREQ("metadata :path: /x/y; metadata initMd1: value1",
            Mock::log.c_str());
    EXPECT_EQ(nullptr, stream.initMdClosure);
    EXPECT_EQ(0U, stream.incoming.size());
    EXPECT_FALSE(trailMdAvail);
}
TEST_F(TestStream, transferData_initialMetadataAddPeerString) {
    stream.initMdClosure = &closure1;
    stream.initMd = &batch;
    stream.streamId.addr.in4.sin_family=AF_INET;
    stream.streamId.addr.in4.sin_addr.s_addr = htonl(0x01020304);
    stream.streamId.addr.in4.sin_port = htons(66);
    stream.isServer = 1;

    stream.incoming.emplace_back(new HomaIncoming(&sock, 1, true,
            0, 0, false, false));
    grpc_core::ExecCtx execCtx;
    stream.transferData();
    execCtx.Flush();
    Mock::log.clear();
    Mock::logMetadata("; ", &batch);
    EXPECT_STREQ("metadata PeerString: ipv4:1.2.3.4:66; "
            "metadata :path: /x/y; "
            "metadata initMd1: value1",
            Mock::log.c_str());
    EXPECT_EQ(0U, stream.incoming.size());
}
TEST_F(TestStream, transferData_initialMetadataSetTrailMdAvail) {
    bool trailMdAvail = false;
    stream.initMdClosure = &closure1;
    stream.initMd = &batch;
    stream.initMdTrailMdAvail = &trailMdAvail;
    stream.incoming.emplace_back(new HomaIncoming(&sock, 1, true,
            0, 0, false, true));
    grpc_core::ExecCtx execCtx;
    stream.transferData();
    execCtx.Flush();
    EXPECT_STREQ("closure1 invoked with 123", Mock::log.c_str());
    Mock::log.clear();
    Mock::logMetadata("; ", &batch);
    EXPECT_STREQ("metadata :path: /x/y; metadata initMd1: value1",
            Mock::log.c_str());
    EXPECT_EQ(nullptr, stream.initMdClosure);
    EXPECT_EQ(1U, stream.incoming.size());
    EXPECT_TRUE(trailMdAvail);
}
TEST_F(TestStream, transferData_waitForInitialMetadataClosure) {
    stream.messageClosure = &closure1;
    absl::optional<grpc_core::SliceBuffer> message;
    stream.messageBody = &message;
    stream.incoming.emplace_back(new HomaIncoming(&sock, 1, true,
            100, 0, false, false));
    grpc_core::ExecCtx execCtx;
    stream.transferData();
    execCtx.Flush();
    EXPECT_STREQ("", Mock::log.c_str());
    EXPECT_EQ(1U, stream.incoming.size());
}
TEST_F(TestStream, transferData_messageData) {
    stream.messageClosure = &closure1;
    absl::optional<grpc_core::SliceBuffer> message;
    stream.messageBody = &message;
    stream.incoming.emplace_back(new HomaIncoming(&sock, 1, false,
            100, 1000, true, false));
    grpc_core::ExecCtx execCtx;
    stream.transferData();
    execCtx.Flush();
    EXPECT_STREQ("closure1 invoked with 123", Mock::log.c_str());
    EXPECT_EQ(nullptr, stream.messageClosure);
    Mock::log.clear();
    Mock::logSliceBuffer("; ", &message.value());
    EXPECT_STREQ("1000-1099", Mock::log.c_str());
    EXPECT_EQ(0U, stream.incoming.size());
}
TEST_F(TestStream, transferData_messageData_multipleChunks) {
    stream.messageClosure = &closure1;
    absl::optional<grpc_core::SliceBuffer> message;
    stream.messageBody = &message;
    stream.incoming.emplace_back(new HomaIncoming(&sock, 1, false,
            100, 1000, true, false));

    // Make the message much larger, but with the same header.
    uint8_t bigBuf[4*HOMA_BPAGE_SIZE];
    HomaIncoming *msg = stream.incoming.back().get();
    char *oldHeader = msg->get<char>(0, nullptr);
    fillData(bigBuf, HOMA_BPAGE_SIZE, 100000);
    fillData(bigBuf + HOMA_BPAGE_SIZE, HOMA_BPAGE_SIZE, 200000);
    fillData(bigBuf + 2*HOMA_BPAGE_SIZE, HOMA_BPAGE_SIZE, 300000);
    fillData(bigBuf + 3*HOMA_BPAGE_SIZE, HOMA_BPAGE_SIZE, 400000);
    msg->recvArgs.num_bpages = 3;
    msg->recvArgs.bpage_offsets[0] = HOMA_BPAGE_SIZE*2;
    msg->recvArgs.bpage_offsets[1] = HOMA_BPAGE_SIZE;
    msg->recvArgs.bpage_offsets[2] = HOMA_BPAGE_SIZE*3;
    msg->length = 160000;
    msg->initMdLength = 100 - sizeof(Wire::Header);
    msg->bodyLength = msg->length - 100;
    msg->sock->bufRegion = bigBuf;
    memcpy(msg->get<char>(0, nullptr), oldHeader, sizeof(Wire::Header));

    grpc_core::ExecCtx execCtx;
    stream.transferData();
    execCtx.Flush();
    EXPECT_STREQ("closure1 invoked with 123", Mock::log.c_str());
    EXPECT_EQ(nullptr, stream.messageClosure);
    Mock::log.clear();
    Mock::logSliceBuffer("; ", &message.value());
    EXPECT_STREQ("300100-365535; 200000-265535; 400000-428927",
            Mock::log.c_str());
    EXPECT_EQ(0U, stream.incoming.size());
}
TEST_F(TestStream, transferData_messageDataInMultipleIncomings) {
    stream.messageClosure = &closure1;
    absl::optional<grpc_core::SliceBuffer> message;
    stream.messageBody = &message;
    stream.incoming.emplace_back(new HomaIncoming(&sock, 1, false,
            100, 1000, false, false));
    grpc_core::ExecCtx execCtx;
    stream.transferData();
    execCtx.Flush();
    EXPECT_STREQ("", Mock::log.c_str());
    EXPECT_EQ(0U, stream.incoming.size());
    EXPECT_EQ(100U, message->Length());
    EXPECT_STREQ("", Mock::log.c_str());

    // Second incoming completes message data.
    uint8_t region2[10000];
    HomaSocket sock2(region2);
    stream.incoming.emplace_back(new HomaIncoming(&sock2, 2, false,
            200, 2000, true, false));
    stream.transferData();
    execCtx.Flush();
    EXPECT_STREQ("closure1 invoked with 123", Mock::log.c_str());
    Mock::log.clear();
    Mock::logSliceBuffer("; ", &message.value());
    EXPECT_STREQ("1000-1099; 2000-2199", Mock::log.c_str());
    EXPECT_EQ(0U, stream.incoming.size());
    message.reset();      // Ensures proper order of memory freeing.
}
TEST_F(TestStream, transferData_zeroLengthMessage) {
    stream.messageClosure = &closure1;
    absl::optional<grpc_core::SliceBuffer> message;
    stream.messageBody = &message;
    stream.incoming.emplace_back(new HomaIncoming(&sock, 1, false,
            0, 1000, true, false));
    grpc_core::ExecCtx execCtx;
    stream.transferData();
    execCtx.Flush();
    EXPECT_STREQ("closure1 invoked with 123", Mock::log.c_str());
    Mock::log.clear();
    Mock::logSliceBuffer("; ", &message.value());
    EXPECT_STREQ("empty block", Mock::log.c_str());
    EXPECT_EQ(0U, stream.incoming.size());
    EXPECT_EQ(0U, message->Length());
    message.reset();      // Ensures proper order of memory freeing.
}
TEST_F(TestStream, transferData_setEof) {
    uint8_t region2[3*HOMA_BPAGE_SIZE], region3[3*HOMA_BPAGE_SIZE];

    // First message: only initial metadata.
    HomaIncoming *msg = new HomaIncoming(&sock, 2, true, 0, 0,
            false, false);
    stream.nextIncomingSequence = 2;
    stream.incoming.emplace_back(msg);
    grpc_core::ExecCtx execCtx;
    stream.transferData();
    execCtx.Flush();
    EXPECT_STREQ("", Mock::log.c_str());
    EXPECT_EQ(1U, stream.incoming.size());
    EXPECT_FALSE(stream.eof);

    // Second message: messageComplete.
    sock.bufRegion = region2;
    msg = new HomaIncoming(&sock, 2, false, 1000, 0, true, false);
    stream.incoming.clear();
    stream.incoming.emplace_back(msg);
    stream.eof = false;
    Mock::log.clear();
    stream.transferData();
    execCtx.Flush();
    EXPECT_STREQ("", Mock::log.c_str());
    EXPECT_EQ(1U, stream.incoming.size());

    // Third message: trailing metadata.
    sock.bufRegion = region3;
    msg = new HomaIncoming(&sock, 2, false, 0, 0, false, true);
    stream.incoming.clear();
    stream.incoming.emplace_back(msg);
    stream.eof = false;
    Mock::log.clear();
    stream.transferData();
    execCtx.Flush();
    EXPECT_STREQ("", Mock::log.c_str());
    EXPECT_EQ(1U, stream.incoming.size());
    EXPECT_TRUE(stream.eof);
}
TEST_F(TestStream, transferData_trailingMetadata) {
    stream.trailMdClosure = &closure1;
    stream.trailMd = &batch;
    stream.incoming.emplace_back(new HomaIncoming(&sock, 1, false,
            0, 0, false, true));
    grpc_core::ExecCtx execCtx;
    stream.transferData();
    execCtx.Flush();
    EXPECT_STREQ("closure1 invoked with 123", Mock::log.c_str());
    Mock::log.clear();
    Mock::logMetadata("; ", &batch);
    EXPECT_STREQ("metadata k2: 0123456789", Mock::log.c_str());
    EXPECT_EQ(0U, stream.incoming.size());
    EXPECT_TRUE(stream.eof);
}
TEST_F(TestStream, transferData_trailingMetadataDontTransfer) {
    // This checks to ensure that trailing metadata doesn't get transferred
    // on servers if trailMdSent is set.
    stream.trailMdClosure = &closure1;
    stream.trailMd = &batch;
    stream.incoming.emplace_back(new HomaIncoming(&sock, 1, false,
            0, 0, false, true));
    stream.isServer = true;
    grpc_core::ExecCtx execCtx;
    stream.transferData();
    execCtx.Flush();
    EXPECT_NSUBSTR("closure", Mock::log.c_str());
    Mock::log.clear();
    EXPECT_EQ(1U, stream.incoming.size());

    // Try again after setting trailMdSent.
    stream.trailMdSent = true;
    stream.transferData();
    execCtx.Flush();
    EXPECT_SUBSTR("closure", Mock::log.c_str());
    Mock::log.clear();
    EXPECT_EQ(0U, stream.incoming.size());
}
TEST_F(TestStream, transferData_multipleMessages) {
    stream.initMdClosure = &closure1;
    stream.initMd = &batch;
    stream.trailMdClosure = &closure2;
    stream.trailMd = &batch2;
    stream.incoming.emplace_back(new HomaIncoming(&sock, 1, true,
            0, 0, false, false));
    uint8_t region2[10000];
    HomaSocket sock2(region2);
    stream.incoming.emplace_back(new HomaIncoming(&sock2, 2, false,
            0, 0, false, true));
    grpc_core::ExecCtx execCtx;
    stream.transferData();
    execCtx.Flush();
    EXPECT_STREQ("closure1 invoked with 123; closure2 invoked with 456",
            Mock::log.c_str());
    Mock::log.clear();
    Mock::logMetadata("; ", &batch);
    EXPECT_STREQ("metadata :path: /x/y; metadata initMd1: value1",
            Mock::log.c_str());
    Mock::log.clear();
    Mock::logMetadata("; ", &batch2);
    EXPECT_STREQ("metadata k2: 0123456789", Mock::log.c_str());
    EXPECT_EQ(0U, stream.incoming.size());

    // This cleanup is needed to ensure that sock2->saveBuffers isn't
    // invoked after the socket is deleted.
    stream.incoming.clear();
    batch.Clear();
    batch2.Clear();
}
TEST_F(TestStream, transferData_useEof) {
    stream.messageClosure = &closure1;
    absl::optional<grpc_core::SliceBuffer> message;
    stream.messageBody = &message;
    stream.eof = true;
    grpc_core::ExecCtx execCtx;
    stream.transferData();
    execCtx.Flush();
    EXPECT_STREQ("closure1 invoked with 123", Mock::log.c_str());
    EXPECT_EQ(nullptr, stream.messageClosure);
    EXPECT_FALSE(message.has_value());
}

TEST_F(TestStream, addPeerToMetadata) {
    stream.streamId.addr.in4.sin_family=AF_INET;
    stream.streamId.addr.in4.sin_addr.s_addr = htonl(0x01020304);
    stream.streamId.addr.in4.sin_port = htons(66);
    Mock::metadataBatchAppend(&batch, "key1", "Two words");
    Mock::metadataBatchAppend(&batch, "key2", "value2");

    EXPECT_TRUE(batch.get_pointer(grpc_core::PeerString()) == NULL);
    stream.addPeerToMetadata(&batch);
    Mock::log.clear();
    Mock::logMetadata("; ", &batch);
    EXPECT_STREQ("metadata PeerString: ipv4:1.2.3.4:66; "
            "metadata key1: Two words; "
            "metadata key2: value2", Mock::log.c_str());
    batch.Clear();
}

TEST_F(TestStream, handleIncoming_respondToOldRequest) {
    HomaIncoming::UniquePtr msg(new HomaIncoming(&sock, 4, true,
            0, 0, false, false));
    msg->hdr()->flags |= (Wire::Header::cancelled | Wire::Header::request);
    stream.homaRequestId = 999;
    stream.handleIncoming(std::move(msg), 444U);
    ASSERT_EQ(1U, Mock::homaMessages.size());
    EXPECT_EQ(0U, stream.incoming.size());
    Mock::log.clear();
    Wire::dumpHeader(Mock::homaMessages[0].data(), GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("gpr_log: Header: id: 33, sequence 0, emptyResponse",
            Mock::log.c_str());
}
TEST_F(TestStream, handleIncoming_cancelled) {
    HomaIncoming::UniquePtr msg(new HomaIncoming(&sock, 4, true,
            0, 0, false, false));
    msg->hdr()->flags |= Wire::Header::cancelled;
    stream.handleIncoming(std::move(msg), 444U);
    EXPECT_EQ(0U, stream.incoming.size());
    EXPECT_STREQ("CANCELLED: ", stream.error.ToString().c_str());
    ASSERT_EQ(0U, Mock::homaMessages.size());
}
TEST_F(TestStream, handleIncoming_basics) {
    stream.initMdClosure = &closure1;
    stream.initMd = &batch;
    stream.trailMdClosure = &closure2;
    stream.trailMd = &batch2;
    grpc_closure closure3;
    GRPC_CLOSURE_INIT(&closure3, closureFunc1,
            reinterpret_cast<void *>(777), dummy);
    stream.messageClosure = &closure3;
    absl::optional<grpc_core::SliceBuffer> message;
    stream.messageBody = &message;
    grpc_core::ExecCtx execCtx;

    // No messages should be processed until all 4 have been passed to
    // handleIncoming.
    HomaIncoming::UniquePtr msg(new HomaIncoming(&sock, 4, false,
            0, 0, false, true));
    stream.handleIncoming(std::move(msg), 444U);
    execCtx.Flush();
    EXPECT_STREQ("", Mock::log.c_str());

    uint8_t region2[5000];
    HomaSocket sock2(region2);
    msg.reset(new HomaIncoming(&sock2, 2, false, 500, 0, false, false));
    stream.handleIncoming(std::move(msg), 445U);
    execCtx.Flush();
    EXPECT_STREQ("", Mock::log.c_str());

    uint8_t region3[5000];
    HomaSocket sock3(region3);
    msg.reset(new HomaIncoming(&sock3, 3, false, 1000, 1500, true, false));
    stream.handleIncoming(std::move(msg), 446U);
    execCtx.Flush();
    EXPECT_STREQ("", Mock::log.c_str());

    ASSERT_EQ(3U, stream.incoming.size());
    EXPECT_EQ(2, stream.incoming[0]->sequence);
    EXPECT_EQ(3, stream.incoming[1]->sequence);
    EXPECT_EQ(4, stream.incoming[2]->sequence);

    uint8_t region4[5000];
    HomaSocket sock4(region4);
    msg.reset(new HomaIncoming(&sock4, 1, true, 0, 0, false, false));
    stream.handleIncoming(std::move(msg), 447U);
    execCtx.Flush();
    EXPECT_STREQ("closure1 invoked with 123; "
            "closure1 invoked with 777; "
            "closure2 invoked with 456", Mock::log.c_str());

    Mock::log.clear();
    Mock::logMetadata("; ", &batch);
    EXPECT_STREQ("metadata :path: /x/y; metadata initMd1: value1",
            Mock::log.c_str());

    Mock::log.clear();
    Mock::logSliceBuffer("; ", &message.value());
    EXPECT_STREQ("0-499; 1500-2499", Mock::log.c_str());

    Mock::log.clear();
    Mock::logMetadata("; ", &batch2);
    EXPECT_STREQ("metadata k2: 0123456789", Mock::log.c_str());

    // This code ensures that saveBuffers doesn't get called for a socket
    // after it has been deleted.
    batch.Clear();
    batch2.Clear();
    message.reset();
    stream.incoming.clear();
}
TEST_F(TestStream, handleIncoming_duplicateLtNextIncomingSequence) {
    stream.initMdClosure = &closure1;
    stream.initMd = &batch;
    stream.nextIncomingSequence = 2;
    grpc_core::ExecCtx execCtx;

    HomaIncoming::UniquePtr msg(new HomaIncoming(&sock, 1, true,
            0, 0, false, false));
    stream.handleIncoming(std::move(msg), 444U);
    execCtx.Flush();
    EXPECT_SUBSTR("Dropping duplicate message", Mock::log.c_str());
}
TEST_F(TestStream, handleIncoming_duplicateQueuedPacket) {
    stream.initMdClosure = &closure1;
    stream.initMd = &batch;
    grpc_core::ExecCtx execCtx;

    HomaIncoming::UniquePtr msg(new HomaIncoming(&sock, 2, true,
            0, 0, false, false));
    stream.handleIncoming(std::move(msg), 444U);
    execCtx.Flush();
    EXPECT_STREQ("", Mock::log.c_str());
    EXPECT_EQ(1U, stream.incoming.size());

    msg.reset(new HomaIncoming(&sock, 2, true, 0, 0, false, false));
    stream.handleIncoming(std::move(msg), 444U);
    execCtx.Flush();
    EXPECT_SUBSTR("Dropping duplicate message", Mock::log.c_str());
    EXPECT_EQ(1U, stream.incoming.size());
}

TEST_F(TestStream, notifyError) {
    grpc_core::ExecCtx execCtx;
    op.recv_initial_metadata = true;
    op.payload->recv_initial_metadata.recv_initial_metadata = &batch;
    op.payload->recv_initial_metadata.recv_initial_metadata_ready = &closure1;

    op.recv_message = true;
    absl::optional<grpc_core::SliceBuffer> message;
    op.payload->recv_message.recv_message = &message;
    grpc_closure closure3;
    GRPC_CLOSURE_INIT(&closure3, closureFunc1,
            reinterpret_cast<void *>(777), dummy);
    op.payload->recv_message.recv_message_ready = &closure3;

    op.recv_trailing_metadata = true;
    op.payload->recv_trailing_metadata.recv_trailing_metadata = &batch;
    op.payload->recv_trailing_metadata.recv_trailing_metadata_ready = &closure2;

    stream.saveCallbacks(&op);
    execCtx.Flush();
    EXPECT_STREQ("", Mock::log.c_str());

    stream.notifyError(GRPC_ERROR_CREATE("testing notifyError"));
    execCtx.Flush();
    EXPECT_EQ(nullptr, stream.initMdClosure);
    EXPECT_EQ(nullptr, stream.messageClosure);
    EXPECT_EQ(nullptr, stream.trailMdClosure);
    EXPECT_SUBSTR("closure1 invoked with 123, error", Mock::log.c_str());
    EXPECT_SUBSTR("closure1 invoked with 777, error", Mock::log.c_str());
    EXPECT_SUBSTR("closure2 invoked with 456, error", Mock::log.c_str());
    EXPECT_SUBSTR("testing notifyError", Mock::log.c_str());
}

TEST_F(TestStream, cancelPeer_peerCancelled) {
    stream.cancelled = true;
    stream.cancelPeer();
    ASSERT_EQ(0U, Mock::homaMessages.size());
}
TEST_F(TestStream, cancelPeer) {
    stream.cancelPeer();
    ASSERT_EQ(1U, Mock::homaMessages.size());
    Wire::dumpHeader(Mock::homaMessages[0].data(), GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("homa_sendv: 1 iovecs, 21 bytes; "
            "gpr_log: Header: id: 33, sequence 1, request, cancelled",
            Mock::log.c_str());
}