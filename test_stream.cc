#include "homa_stream.h"
#include "mock.h"

class TestStream : public ::testing::Test {
public:
    grpc_core::Arena *arena;
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
        if (error != GRPC_ERROR_NONE) {
            Mock::logPrintf("; ", "closure1 invoked with %ld, error %s",
                    value, grpc_error_string(error));
        } else {
            Mock::logPrintf("; ", "closure1 invoked with %ld", value);
        }
    }
    
    static void closureFunc2(void* arg, grpc_error_handle error) {
        int64_t value = reinterpret_cast<int64_t>(arg);
        if (error != GRPC_ERROR_NONE) {
            Mock::logPrintf("; ", "closure2 invoked with %ld, error %s",
                    value, grpc_error_string(error));
        } else {
            Mock::logPrintf("; ", "closure2 invoked with %ld", value);
        }
    }
    
    TestStream()
        : arena(grpc_core::Arena::Create(2000))
        , refcount()
        , streamId(33)
        , stream(streamId, 111, 3, &refcount, arena)
        , closure1()
        , closure2()
        , batch()
        , batch2()
        , op()
        , payload(nullptr)
    {
        Mock::setUp();
        stream.newXmit();
        GRPC_CLOSURE_INIT(&closure1, closureFunc1,
                reinterpret_cast<void *>(123), dummy);
        GRPC_CLOSURE_INIT(&closure2, closureFunc2,
                reinterpret_cast<void *>(456), dummy);
        grpc_metadata_batch_init(&batch);
        grpc_metadata_batch_init(&batch2);
        op.payload = &payload;
    }
    
    ~TestStream()
    {
        grpc_metadata_batch_destroy(&batch);
        grpc_metadata_batch_destroy(&batch2);
    }
};

TEST_F(TestStream, flush_noMessage) {
    stream.flush();
    EXPECT_STREQ("", Mock::log.c_str());
}
TEST_F(TestStream, flush_sendRequest) {
    stream.homaId = 0;
    stream.hdr()->flags |= Wire::Header::initMdPresent;
    stream.flush();
    EXPECT_SUBSTR("homa_sendv: 1 iovecs", Mock::log.c_str());
}
TEST_F(TestStream, flush_sendReply) {
    stream.hdr()->flags |= Wire::Header::trailMdPresent;
    stream.flush();
    EXPECT_SUBSTR("homa_replyv: 1 iovecs", Mock::log.c_str());
}
TEST_F(TestStream, flush_error) {
    stream.hdr()->flags |= Wire::Header::trailMdPresent;
    Mock::homaReplyvErrors = 1;
    stream.flush();
    EXPECT_SUBSTR("gpr_log: Couldn't send Homa response: Input/output error",
            Mock::log.c_str());
}

TEST_F(TestStream, metadataLength) {
    Mock::metadataBatchAppend(&batch, "key1", "7 chars", arena);
    Mock::metadataBatchAppend(&batch, "k2", "0123456789", arena);
    EXPECT_EQ(2*sizeof(Wire::Mdata) + 23, HomaStream::metadataLength(&batch));
    EXPECT_STREQ("", Mock::log.c_str());
}

TEST_F(TestStream, serializeMetadata_basics) {
    Mock::metadataBatchAppend(&batch, "key1", "7 chars", arena);
    Mock::metadataBatchAppend(&batch, "k2", "0123456789", arena);
    size_t initialSize = stream.xmitSize;
    stream.serializeMetadata(&batch);
    size_t length = stream.xmitSize - initialSize;
    EXPECT_EQ(2*sizeof(Wire::Mdata) + 23, length);
    Wire::dumpMetadata(stream.xmitBuffer + initialSize, length,
            GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("gpr_log: Key: key1, value: 7 chars, index: 24; "
            "gpr_log: Key: k2, value: 0123456789, index: 24",
            Mock::log.c_str());
}
TEST_F(TestStream, serializeMetadata_chunkOverflow) {
    Mock::metadataBatchAppend(&batch, "key1", "7 chars", arena);
    Mock::metadataBatchAppend(&batch, "k2", "0123456789", arena);
    size_t initialSize = stream.xmitSize;
    stream.lastVecAvail = 30;
    stream.serializeMetadata(&batch);
    EXPECT_EQ(2U, stream.vecs.size());
    size_t length = sizeof(Wire::Mdata) + 11;
    EXPECT_EQ(length + initialSize, stream.vecs[0].iov_len);
    Wire::dumpMetadata(stream.xmitBuffer + initialSize, length,
            GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("gpr_log: Key: key1, value: 7 chars, index: 24",
            Mock::log.c_str());
    EXPECT_EQ(sizeof(Wire::Mdata) + 12, stream.vecs[1].iov_len);
    Mock::log.clear();
    Wire::dumpMetadata(stream.vecs[1].iov_base, stream.vecs[1].iov_len,
            GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("gpr_log: Key: k2, value: 0123456789, index: 24",
            Mock::log.c_str());
}

TEST_F(TestStream, xmit_initialMetadata) {
    op.send_initial_metadata = true;
    payload.send_initial_metadata.send_initial_metadata = &batch;
    Mock::metadataBatchAppend(&batch, "key1", "7 chars", arena);
    Mock::metadataBatchAppend(&batch, "k2", "0123456789", arena);
    stream.xmit(&op);
    ASSERT_EQ(1U, Mock::homaMessages.size());
    Wire::dumpHeader(Mock::homaMessages[0].data(), GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("homa_replyv: 1 iovecs, 62 bytes; "
            "gpr_log: Header: id: 33, sequence 2, initMdBytes 41, initMdPresent",
            Mock::log.c_str());
    Mock::log.clear();
    Wire::dumpMetadata(Mock::homaMessages[0].data() + sizeof(Wire::Header),
            Mock::homaMessages[0].size() - sizeof(Wire::Header),
            GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("gpr_log: Key: key1, value: 7 chars, index: 24; "
            "gpr_log: Key: k2, value: 0123456789, index: 24",
            Mock::log.c_str());
    
}
TEST_F(TestStream, xmit_initialMetadataTooLarge) {
    op.send_initial_metadata = true;
    payload.send_initial_metadata.send_initial_metadata = &batch;
    Mock::metadataBatchAppend(&batch, "key1", "7 chars", arena);
    Mock::metadataBatchAppend(&batch, "k2", "0123456789", arena);
    stream.maxMessageLength = 50;
    stream.xmit(&op);
    EXPECT_EQ(0U, Mock::homaMessages.size());
    EXPECT_SUBSTR("Too much initial metadata",
            Mock::log.c_str());
    EXPECT_SUBSTR("Too much initial metadata",
            grpc_error_std_string(stream.error).c_str());
    
}
TEST_F(TestStream, xmit_trailingMetadataTooLarge) {
    op.send_trailing_metadata = true;
    payload.send_trailing_metadata.send_trailing_metadata = &batch;
    Mock::metadataBatchAppend(&batch, "key1", "7 chars", arena);
    Mock::metadataBatchAppend(&batch, "k2", "0123456789", arena);
    stream.maxMessageLength = 50;
    stream.xmit(&op);
    EXPECT_EQ(0U, Mock::homaMessages.size());
    EXPECT_SUBSTR("Too much trailing metadata",
            Mock::log.c_str());
    EXPECT_SUBSTR("Too much trailing metadata",
            grpc_error_std_string(stream.error).c_str());
    
}
TEST_F(TestStream, xmit_onlyTrailingMetadata) {
    op.send_trailing_metadata = true;
    payload.send_trailing_metadata.send_trailing_metadata = &batch;
    Mock::metadataBatchAppend(&batch, "key1", "7 chars", arena);
    Mock::metadataBatchAppend(&batch, "k2", "0123456789", arena);
    stream.xmit(&op);
    ASSERT_EQ(1U, Mock::homaMessages.size());
    Wire::dumpHeader(Mock::homaMessages[0].data(), GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("homa_replyv: 1 iovecs, 62 bytes; "
            "gpr_log: Header: id: 33, sequence 2, trailMdBytes 41, "
            "trailMdPresent",
            Mock::log.c_str());
    Mock::log.clear();
    Wire::dumpMetadata(Mock::homaMessages[0].data() + sizeof(Wire::Header),
            Mock::homaMessages[0].size() - sizeof(Wire::Header),
            GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("gpr_log: Key: key1, value: 7 chars, index: 24; "
            "gpr_log: Key: k2, value: 0123456789, index: 24",
            Mock::log.c_str());
}
TEST_F(TestStream, xmit_onlyMessageData) {
    op.send_message = true;
    grpc_slice_buffer slices;
    grpc_slice_buffer_init(&slices);
    grpc_slice_buffer_add(&slices, Mock::dataSlice(100, 1000));
    grpc_slice_buffer_add(&slices, Mock::dataSlice(200, 2000));
    grpc_slice_buffer_add(&slices, Mock::dataSlice(300, 3000));
    payload.send_message.send_message.reset(
            new grpc_core::SliceBufferByteStream(&slices, 0));
    stream.xmit(&op);
    ASSERT_EQ(1U, Mock::homaMessages.size());
    Wire::dumpHeader(Mock::homaMessages[0].data(), GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("homa_replyv: 4 iovecs, 621 bytes; "
            "gpr_log: Header: id: 33, sequence 2, messageBytes 600, "
            "messageComplete",
            Mock::log.c_str());
    Mock::log.clear();
    Mock::logData("; ",
            Mock::homaMessages[0].data() + sizeof(Wire::Header),
            Mock::homaMessages[0].size() - sizeof(Wire::Header));
    EXPECT_STREQ("1000-1099 2000-2199 3000-3299",
            Mock::log.c_str());
    grpc_slice_buffer_destroy(&slices);
}
TEST_F(TestStream, xmit_everything) {
    op.send_initial_metadata = true;
    payload.send_initial_metadata.send_initial_metadata = &batch;
    Mock::metadataBatchAppend(&batch, "key1", "7 chars", arena);
    
    op.send_message = true;
    grpc_slice_buffer slices;
    grpc_slice_buffer_init(&slices);
    grpc_slice_buffer_add(&slices, Mock::dataSlice(100, 1000));
    grpc_slice_buffer_add(&slices, Mock::dataSlice(200, 2000));
    grpc_slice_buffer_add(&slices, Mock::dataSlice(300, 3000));
    payload.send_message.send_message.reset(
            new grpc_core::SliceBufferByteStream(&slices, 0));
    
    op.send_trailing_metadata = true;
    payload.send_trailing_metadata.send_trailing_metadata = &batch2;
    Mock::metadataBatchAppend(&batch2, "k2", "0123456789", arena);
    
    stream.xmit(&op);
    ASSERT_EQ(1U, Mock::homaMessages.size());
    Wire::dumpHeader(Mock::homaMessages[0].data(), GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("homa_replyv: 4 iovecs, 662 bytes; "
            "gpr_log: Header: id: 33, sequence 2, initMdBytes 20, "
            "messageBytes 600, trailMdBytes 21, initMdPresent, "
            "messageComplete, trailMdPresent",
            Mock::log.c_str());
    Mock::log.clear();
    Wire::dumpMetadata(Mock::homaMessages[0].data() + sizeof(Wire::Header),
            20, GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("gpr_log: Key: key1, value: 7 chars, index: 24",
            Mock::log.c_str());
    Mock::log.clear();
    Wire::dumpMetadata(Mock::homaMessages[0].data()
            + sizeof(Wire::Header) + 20,
            21, GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("gpr_log: Key: k2, value: 0123456789, index: 24",
            Mock::log.c_str());
    Mock::log.clear();
    Mock::logData("; ",
            Mock::homaMessages[0].data() + sizeof(Wire::Header) + 41,
            Mock::homaMessages[0].size() - (sizeof(Wire::Header) + 41));
    EXPECT_STREQ("1000-1099 2000-2199 3000-3299",
            Mock::log.c_str());
    grpc_slice_buffer_destroy(&slices);
}
TEST_F(TestStream, xmit_multipleHomaMessages) {
    op.send_initial_metadata = true;
    payload.send_initial_metadata.send_initial_metadata = &batch;
    Mock::metadataBatchAppend(&batch, "key1", "7 chars", arena);
    
    op.send_message = true;
    grpc_slice_buffer slices;
    grpc_slice_buffer_init(&slices);
    grpc_slice_buffer_add(&slices, Mock::dataSlice(100, 1000));
    grpc_slice_buffer_add(&slices, Mock::dataSlice(500, 2000));
    grpc_slice_buffer_add(&slices, Mock::dataSlice(100, 3000));
    payload.send_message.send_message.reset(
            new grpc_core::SliceBufferByteStream(&slices, 0));
    
    op.send_trailing_metadata = true;
    payload.send_trailing_metadata.send_trailing_metadata = &batch2;
    Mock::metadataBatchAppend(&batch2, "k2", "0123456789", arena);
    
    stream.maxMessageLength = 301;
    stream.xmit(&op);
    ASSERT_EQ(3U, Mock::homaMessages.size());
    EXPECT_STREQ("homa_replyv: 3 iovecs, 301 bytes; "
            "homa_replyv: 2 iovecs, 301 bytes; "
            "homa_replyv: 3 iovecs, 202 bytes",
            Mock::log.c_str());
    
    // First Homa message: initial metadata plus some message data.
    Mock::log.clear();
    Wire::dumpHeader(Mock::homaMessages[0].data(), GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("gpr_log: Header: id: 33, sequence 2, initMdBytes 20, "
            "messageBytes 260, initMdPresent",
            Mock::log.c_str());
    Mock::log.clear();
    Wire::dumpMetadata(Mock::homaMessages[0].data() + sizeof(Wire::Header),
            20, GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("gpr_log: Key: key1, value: 7 chars, index: 24",
            Mock::log.c_str());
    Mock::log.clear();
    Mock::logData("; ",
            Mock::homaMessages[0].data() + sizeof(Wire::Header) + 20,
            Mock::homaMessages[0].size() - (sizeof(Wire::Header) + 20));
    EXPECT_STREQ("1000-1099 2000-2159", Mock::log.c_str());
    
    // Second Homa message: more message data.
    Mock::log.clear();
    Wire::dumpHeader(Mock::homaMessages[1].data(), GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("gpr_log: Header: id: 33, sequence 3, messageBytes 280",
            Mock::log.c_str());
    Mock::log.clear();
    Mock::logData("; ", Mock::homaMessages[1].data() + sizeof(Wire::Header),
            Mock::homaMessages[1].size() - (sizeof(Wire::Header)));
    EXPECT_STREQ("2160-2439", Mock::log.c_str());
    
    // Third Homa message; last message data plus trailing metadata.
    Mock::log.clear();
    Wire::dumpHeader(Mock::homaMessages[2].data(), GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("gpr_log: Header: id: 33, sequence 4, messageBytes 160, "
            "trailMdBytes 21, messageComplete, trailMdPresent",
            Mock::log.c_str());
    Mock::log.clear();
    Wire::dumpMetadata(Mock::homaMessages[2].data() + sizeof(Wire::Header),
            21, GPR_LOG_SEVERITY_ERROR);
    EXPECT_STREQ("gpr_log: Key: k2, value: 0123456789, index: 24",
            Mock::log.c_str());
    Mock::log.clear();
    Mock::logData("; ",
            Mock::homaMessages[2].data() + sizeof(Wire::Header) + 21,
            Mock::homaMessages[2].size() - (sizeof(Wire::Header) + 21));
    EXPECT_STREQ("2440-2499 3000-3099", Mock::log.c_str());
    
    grpc_slice_buffer_destroy(&slices);
}

TEST_F(TestStream, transferData_outOfSequence) {
    stream.initMdClosure = &closure1;
    stream.initMd = &batch;
    HomaIncoming *msg = new HomaIncoming(2, true, 0, 0, 0, false, false);
    stream.incoming.emplace_back(msg);
    grpc_core::ExecCtx execCtx;
    stream.transferData();
    execCtx.Flush();
    EXPECT_STREQ("", Mock::log.c_str());
    EXPECT_EQ(1U, stream.incoming.size());
}
TEST_F(TestStream, transferData_initialMetadata) {
    stream.initMdClosure = &closure1;
    stream.initMd = &batch;
    stream.incoming.emplace_back(new HomaIncoming(1, true, 0, 0, 0, false,
            false));
    grpc_core::ExecCtx execCtx;
    stream.transferData();
    execCtx.Flush();
    EXPECT_STREQ("closure1 invoked with 123", Mock::log.c_str());
    Mock::log.clear();
    Mock::logMetadata("; ", &batch);
    EXPECT_STREQ("metadata initMd1: value1 (24); metadata :path: /x/y (0)",
            Mock::log.c_str());
    EXPECT_EQ(nullptr, stream.initMdClosure);
    EXPECT_EQ(0U, stream.incoming.size());
}
TEST_F(TestStream, transferData_waitForInitialMetadataClosure) {
    stream.messageClosure = &closure1;
    grpc_core::OrphanablePtr<grpc_core::ByteStream> message;
    stream.messageStream = &message;
    stream.incoming.emplace_back(new HomaIncoming(1, true, 100, 0, 0, false,
            false));
    grpc_core::ExecCtx execCtx;
    stream.transferData();
    execCtx.Flush();
    EXPECT_STREQ("", Mock::log.c_str());
    EXPECT_EQ(1U, stream.incoming.size());
}
TEST_F(TestStream, transferData_messageData) {
    stream.messageClosure = &closure1;
    grpc_core::OrphanablePtr<grpc_core::ByteStream> message;
    stream.messageStream = &message;
    stream.incoming.emplace_back(new HomaIncoming(1, false, 100, 0, 1000, true,
            false));
    grpc_core::ExecCtx execCtx;
    stream.transferData();
    execCtx.Flush();
    EXPECT_STREQ("closure1 invoked with 123", Mock::log.c_str());
    EXPECT_EQ(nullptr, stream.messageClosure);
    Mock::log.clear();
    Mock::logByteStream("; ", message.get());
    EXPECT_STREQ("1000-1099", Mock::log.c_str());
    EXPECT_EQ(0U, stream.incoming.size());
}
TEST_F(TestStream, transferData_messageDataAlsoInTail) {
    stream.messageClosure = &closure1;
    grpc_core::OrphanablePtr<grpc_core::ByteStream> message;
    stream.messageStream = &message;
    stream.incoming.emplace_back(new HomaIncoming(1, false, 100, 5000, 1000,
            true, false));
    grpc_core::ExecCtx execCtx;
    stream.transferData();
    execCtx.Flush();
    EXPECT_STREQ("closure1 invoked with 123", Mock::log.c_str());
    EXPECT_EQ(0U, stream.incoming.size());
    Mock::log.clear();
    Mock::logByteStream("; ", message.get());
    EXPECT_STREQ("1000-1099; 1100-6099", Mock::log.c_str());
}
TEST_F(TestStream, transferData_mustWaitForMessageClosure) {
    stream.trailMdClosure = &closure1;
    stream.trailMd = &batch;
    stream.incoming.emplace_back(new HomaIncoming(1, false, 100, 5000, 1000,
            true, true));
    grpc_core::ExecCtx execCtx;
    stream.transferData();
    execCtx.Flush();
    EXPECT_STREQ("", Mock::log.c_str());
    Mock::log.clear();
    Mock::logMetadata("; ", &batch);
    EXPECT_STREQ("", Mock::log.c_str());
    EXPECT_EQ(1U, stream.incoming.size());
}
TEST_F(TestStream, transferData_trailingMetadata) {
    stream.trailMdClosure = &closure1;
    stream.trailMd = &batch;
    stream.incoming.emplace_back(new HomaIncoming(1, false, 0, 0, 0, false,
            true));
    grpc_core::ExecCtx execCtx;
    stream.transferData();
    execCtx.Flush();
    EXPECT_STREQ("closure1 invoked with 123", Mock::log.c_str());
    Mock::log.clear();
    Mock::logMetadata("; ", &batch);
    EXPECT_STREQ("metadata k2: 0123456789 (24)", Mock::log.c_str());
    EXPECT_EQ(0U, stream.incoming.size());
}
TEST_F(TestStream, transferData_mustWaitForTrailingMetadataClosure) {
    stream.incoming.emplace_back(new HomaIncoming(1, false, 0, 0, 0, false,
            true));
    grpc_core::ExecCtx execCtx;
    stream.transferData();
    execCtx.Flush();
    EXPECT_STREQ("", Mock::log.c_str());
    Mock::log.clear();
    Mock::logMetadata("; ", &batch);
    EXPECT_STREQ("", Mock::log.c_str());
    EXPECT_EQ(1U, stream.incoming.size());
}
TEST_F(TestStream, transferData_multipleMessages) {
    stream.initMdClosure = &closure1;
    stream.initMd = &batch;
    stream.trailMdClosure = &closure2;
    stream.trailMd = &batch2;
    stream.incoming.emplace_back(new HomaIncoming(1, true, 0, 0, 0, false,
            false));
    stream.incoming.emplace_back(new HomaIncoming(2, false, 0, 0, 0, false,
            true));
    grpc_core::ExecCtx execCtx;
    stream.transferData();
    execCtx.Flush();
    EXPECT_STREQ("closure1 invoked with 123; closure2 invoked with 456",
            Mock::log.c_str());
    Mock::log.clear();
    Mock::logMetadata("; ", &batch);
    EXPECT_STREQ("metadata initMd1: value1 (24); metadata :path: /x/y (0)",
            Mock::log.c_str());
    Mock::log.clear();
    Mock::logMetadata("; ", &batch2);
    EXPECT_STREQ("metadata k2: 0123456789 (24)", Mock::log.c_str());
    EXPECT_EQ(0U, stream.incoming.size());
}
TEST_F(TestStream, handleIncoming) {
    stream.initMdClosure = &closure1;
    stream.initMd = &batch;
    stream.trailMdClosure = &closure2;
    stream.trailMd = &batch2;
    grpc_closure closure3;
    GRPC_CLOSURE_INIT(&closure3, closureFunc1,
            reinterpret_cast<void *>(777), dummy);
    stream.messageClosure = &closure3;
    grpc_core::OrphanablePtr<grpc_core::ByteStream> message;
    stream.messageStream = &message;
    grpc_core::ExecCtx execCtx;
    
    // No messages should be processed until all 4 have been passed to
    // handleIncoming.
    HomaIncoming::UniquePtr msg(new HomaIncoming(4, false, 0, 0, 0, false,
            true));
    stream.handleIncoming(std::move(msg));
    execCtx.Flush();
    EXPECT_STREQ("", Mock::log.c_str());
    
    msg.reset(new HomaIncoming(2, false, 500, 1000, 0, false, false));
    stream.handleIncoming(std::move(msg));
    execCtx.Flush();
    EXPECT_STREQ("", Mock::log.c_str());
    
    msg.reset(new HomaIncoming(3, false, 1000, 0, 1500, true, false));
    stream.handleIncoming(std::move(msg));
    execCtx.Flush();
    EXPECT_STREQ("", Mock::log.c_str());
    
    ASSERT_EQ(3U, stream.incoming.size());
    EXPECT_EQ(2, stream.incoming[0]->sequence);
    EXPECT_EQ(3, stream.incoming[1]->sequence);
    EXPECT_EQ(4, stream.incoming[2]->sequence);
    
    msg.reset(new HomaIncoming(1, true, 0, 0, 0, false, false));
    stream.handleIncoming(std::move(msg));
    execCtx.Flush();
    EXPECT_STREQ("closure1 invoked with 123; "
            "closure1 invoked with 777; "
            "closure2 invoked with 456", Mock::log.c_str());
    
    Mock::log.clear();
    Mock::logMetadata("; ", &batch);
    EXPECT_STREQ("metadata initMd1: value1 (24); metadata :path: /x/y (0)",
            Mock::log.c_str());
    
    Mock::log.clear();
    Mock::logByteStream("; ", message.get());
    EXPECT_STREQ("0-499; 500-1499; 1500-2499", Mock::log.c_str());
    
    Mock::log.clear();
    Mock::logMetadata("; ", &batch2);
    EXPECT_STREQ("metadata k2: 0123456789 (24)", Mock::log.c_str());
}