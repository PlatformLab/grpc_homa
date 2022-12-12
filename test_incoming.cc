#include "homa_incoming.h"
#include "mock.h"
#include "util.h"

// This file contains unit tests for homa_incoming.cc and homa_incoming.h.

class TestIncoming : public ::testing::Test {
public:
    HomaSocket sock;
    uint64_t homaId;
    uint8_t bigBuf[4*HOMA_BPAGE_SIZE];

    TestIncoming()
        : sock(Mock::bufRegion)
        , homaId(0)
        , bigBuf()
    {
        Mock::setUp();
    }

    /**
     * Modifies a message to use a larger buffer region, and fills the
     * message with well-known values.
     * \param msg
     *      Message to modify to use the large region.
     * \param length
     *      New length for the message.
     */
    void setBigBuf(HomaIncoming *msg, size_t length)
    {
        memset(bigBuf, 0, HOMA_BPAGE_SIZE);
        memset(bigBuf + HOMA_BPAGE_SIZE, 0, HOMA_BPAGE_SIZE);
        memset(bigBuf + 2*HOMA_BPAGE_SIZE, 0, HOMA_BPAGE_SIZE);
        memset(bigBuf + 3*HOMA_BPAGE_SIZE, 0, HOMA_BPAGE_SIZE);
        msg->control.num_bpages = (length + HOMA_BPAGE_SIZE - 1)
                >> HOMA_BPAGE_SHIFT;
        msg->control.bpage_offsets[0] = HOMA_BPAGE_SIZE*2;
        msg->control.bpage_offsets[1] = HOMA_BPAGE_SIZE;
        msg->control.bpage_offsets[2] = HOMA_BPAGE_SIZE*3;
        msg->length = length;
        msg->sock->bufRegion = bigBuf;
        size_t offset = 0;
        while (offset < length) {
            size_t chunkSize = msg->contiguous(offset);
            if (chunkSize > (length - offset)) {
                chunkSize = length - offset;
            }
            fillData(msg->get<char>(offset, nullptr), chunkSize, offset);
            offset += chunkSize;
        }
    }
};

TEST_F(TestIncoming, destructor_saveBuffers) {
    grpc_error_handle error;
    HomaIncoming::UniquePtr msg = HomaIncoming::read(&sock, 5, &homaId, &error);
    ASSERT_EQ(GRPC_ERROR_NONE, error);
    msg->control.num_bpages = 2;
    msg->control.bpage_offsets[0] = 123;
    msg->control.bpage_offsets[1] = 456;
    msg->sliceRefs.Unref();
    ASSERT_EQ(2U, sock.savedBuffers.size());
    EXPECT_EQ(123U, sock.savedBuffers.at(0));
    EXPECT_EQ(456U, sock.savedBuffers.at(1));
}

TEST_F(TestIncoming, read_basics) {
    grpc_error_handle error;
    sock.savedBuffers.push_back(111);
    sock.savedBuffers.push_back(222);
    sock.savedBuffers.push_back(333);
    HomaIncoming::UniquePtr msg = HomaIncoming::read(&sock, 5, &homaId, &error);
    ASSERT_TRUE(msg);
    EXPECT_EQ(44U, msg->streamId.id);
    EXPECT_EQ(1000U, msg->bodyLength);
    EXPECT_EQ(1051U, msg->length);
    EXPECT_EQ(44444U, msg->control.completion_cookie);
    EXPECT_EQ(1U, msg->control.num_bpages);
    EXPECT_EQ(GRPC_ERROR_NONE, error);
    EXPECT_EQ(3, Mock::buffersReturned);
    EXPECT_EQ(0U, sock.savedBuffers.size());
}
TEST_F(TestIncoming, read_noMessageAvailable) {
    grpc_error_handle error;
    Mock::recvmsgErrors = 1;
    Mock::errorCode = EAGAIN;
    HomaIncoming::UniquePtr msg = HomaIncoming::read(&sock, 5, &homaId, &error);
    EXPECT_EQ(GRPC_ERROR_NONE, error);
    EXPECT_EQ(nullptr, msg.get());
    EXPECT_STREQ("", Mock::log.c_str());
}
TEST_F(TestIncoming, read_recvmsgFails) {
    grpc_error_handle error;
    Mock::recvmsgErrors = 1;
    gpr_set_log_verbosity(GPR_LOG_SEVERITY_DEBUG);
    HomaIncoming::UniquePtr msg = HomaIncoming::read(&sock, 5, &homaId, &error);
    EXPECT_NE(GRPC_ERROR_NONE, error);
    EXPECT_EQ(nullptr, msg.get());
    EXPECT_SUBSTR("gpr_log: Error in recvmsg", Mock::log.c_str());
    EXPECT_SUBSTR("os_error", grpc_error_string(error));
}
TEST_F(TestIncoming, read_messageTooShort) {
    grpc_error_handle error;
    Mock::recvmsgLengths.push_back(4);
    HomaIncoming::UniquePtr msg = HomaIncoming::read(&sock, 5, &homaId, &error);
    EXPECT_NE(GRPC_ERROR_NONE, error);
    EXPECT_EQ(nullptr, msg.get());
    EXPECT_SUBSTR("gpr_log: Homa message contained only 4 bytes",
            Mock::log.c_str());
    EXPECT_SUBSTR("Incoming Homa message too short for header",
            grpc_error_string(error));
}
TEST_F(TestIncoming, read_discardStreamingResponses) {
    Mock::recvmsgHeaders.emplace_back(1, 2);
    Mock::recvmsgHeaders.emplace_back(1, 3);
    Mock::recvmsgHeaders.emplace_back(2, 1);
    Mock::recvmsgHeaders[0].flags |= Wire::Header::emptyResponse;
    Mock::recvmsgHeaders[1].flags |= Wire::Header::emptyResponse;
    grpc_error_handle error;
    HomaIncoming::UniquePtr msg = HomaIncoming::read(&sock, 5, &homaId, &error);
    EXPECT_EQ(GRPC_ERROR_NONE, error);
    EXPECT_EQ(2U, msg->streamId.id);
    EXPECT_EQ(1, msg->sequence);
    EXPECT_EQ(0U, Mock::recvmsgHeaders.size());
}
TEST_F(TestIncoming, read_lengthsInconsistent) {
    grpc_error_handle error;
    Mock::recvmsgLengths.push_back(1000);
    HomaIncoming::UniquePtr msg = HomaIncoming::read(&sock, 5, &homaId, &error);
    EXPECT_NE(GRPC_ERROR_NONE, error);
    EXPECT_EQ(nullptr, msg.get());
    EXPECT_SUBSTR("gpr_log: Bad message length 1000",
            Mock::log.c_str());
    EXPECT_SUBSTR("Incoming Homa message length doesn't match header",
            grpc_error_string(error));
}

TEST_F(TestIncoming, copyOut) {
    grpc_error_handle error;
    HomaIncoming::UniquePtr msg = HomaIncoming::read(&sock, 5, &homaId, &error);
    EXPECT_EQ(GRPC_ERROR_NONE, error);
    setBigBuf(msg.get(), 2*HOMA_BPAGE_SIZE + 10000);

    // First block is entirely within a bpage.
    char buffer[100000];
    Mock::log.clear();
    msg->copyOut(buffer, 80000, 40);
    Mock::logData("; ", buffer, 40);
    EXPECT_STREQ("80000-80039", Mock::log.c_str());

    // Second block crosses a bpage boundary.
    Mock::log.clear();
    msg->copyOut(buffer, 60000, 10000);
    Mock::logData("; ", buffer, 10000);
    EXPECT_STREQ("60000-69999", Mock::log.c_str());

    // Third block crosses multiple boundaries.
    Mock::log.clear();
    msg->copyOut(buffer, 20000, sizeof(buffer));
    Mock::logData("; ", buffer, sizeof(buffer));
    EXPECT_STREQ("20000-119999",
            Mock::log.c_str());

    // Fourth block: past end of message.
    Mock::log.clear();
    memset(buffer, 0, sizeof(buffer));
    msg->copyOut(buffer, 2*HOMA_BPAGE_SIZE + 10000, 20);
    Mock::logData("; ", buffer, 20);
    EXPECT_STREQ("0-3 0-3 0-3 0-3 0-3", Mock::log.c_str());
}

TEST_F(TestIncoming, getStaticSlice) {
    grpc_error_handle error;
    grpc_core::Arena *arena = grpc_core::Arena::Create(2000);
    HomaIncoming::UniquePtr msg = HomaIncoming::read(&sock, 5, &homaId, &error);
    EXPECT_EQ(GRPC_ERROR_NONE, error);
    fillData(msg->get<char>(0, nullptr), 500, 0);

    // First slice is small enough to be stored internally.
    grpc_slice slice1 = msg->getStaticSlice(60, 8, arena);
    Mock::logData("; ", GRPC_SLICE_START_PTR(slice1), GRPC_SLICE_LENGTH(slice1));
    EXPECT_STREQ("60-67", Mock::log.c_str());
    EXPECT_EQ(nullptr, slice1.refcount);

    // Second slice is allocated in the arena.
    Mock::log.clear();
    grpc_slice slice2 = msg->getStaticSlice(100, 200, arena);
    Mock::logData("; ", GRPC_SLICE_START_PTR(slice2), GRPC_SLICE_LENGTH(slice2));
    EXPECT_STREQ("100-299", Mock::log.c_str());
    EXPECT_EQ(&grpc_core::kNoopRefcount, slice2.refcount);

    arena->Destroy();
}

TEST_F(TestIncoming, getSlice) {
    grpc_error_handle error;
    int destroyCounter = 0;
    HomaIncoming::UniquePtr msg = HomaIncoming::read(&sock, 5, &homaId, &error);
    EXPECT_EQ(GRPC_ERROR_NONE, error);
    msg->destroyCounter = &destroyCounter;
    setBigBuf(msg.get(), HOMA_BPAGE_SIZE + 10000);

    // First slice is in contiguous in the message.
    grpc_slice slice1 = msg->getSlice(1000, 200);
    Mock::logData("; ", GRPC_SLICE_START_PTR(slice1), GRPC_SLICE_LENGTH(slice1));
    EXPECT_STREQ("1000-1199", Mock::log.c_str());

    // Second slice crosses a bpage boundary.
    Mock::log.clear();
    grpc_slice slice2 = msg->getSlice(60000, 10000);
    Mock::logData("; ", GRPC_SLICE_START_PTR(slice2), GRPC_SLICE_LENGTH(slice2));
    EXPECT_STREQ("60000-69999", Mock::log.c_str());

    // Now make sure that the reference counting worked correctly.
    EXPECT_EQ(0, destroyCounter);
    msg.reset(nullptr);
    EXPECT_EQ(0, destroyCounter);
    grpc_slice_unref(slice2);
    EXPECT_EQ(0, destroyCounter);
    grpc_slice_unref(slice1);
    EXPECT_EQ(1, destroyCounter);
}

TEST_F(TestIncoming, deserializeMetadata_basics) {
    grpc_error_handle error;
    grpc_core::Arena *arena = grpc_core::Arena::Create(2000);
    HomaIncoming::UniquePtr msg = HomaIncoming::read(&sock, 5, &homaId, &error);
    EXPECT_EQ(GRPC_ERROR_NONE, error);
    ASSERT_TRUE(msg);
    int destroyCounter = 0;
    msg->destroyCounter = &destroyCounter;
    size_t length = msg->addMetadata(75,
            "name1", "value1",
            "name2", "value2",
            "n3", "abcdefghijklmnop", nullptr);
    grpc_metadata_batch batch(arena);
    msg->deserializeMetadata(75, length, &batch, arena);
    Mock::logMetadata("; ", &batch);
    EXPECT_STREQ("metadata name1: value1; "
            "metadata name2: value2; "
            "metadata n3: abcdefghijklmnop", Mock::log.c_str());
    msg.reset(nullptr);
    EXPECT_EQ(1, destroyCounter);
    batch.Clear();
    arena->Destroy();
}
TEST_F(TestIncoming, deserializeMetadata_metadataOverrunsSpace) {
    grpc_error_handle error;
    grpc_core::Arena *arena = grpc_core::Arena::Create(2000);
    HomaIncoming::UniquePtr msg = HomaIncoming::read(&sock, 5, &homaId, &error);
    EXPECT_EQ(GRPC_ERROR_NONE, error);
    ASSERT_TRUE(msg);
    size_t length = msg->addMetadata(75,
            "name1", "value1",
            "name2", "value2",
            "n3", "abcdefghijklmnop", nullptr);
    grpc_metadata_batch batch(arena);
    msg->deserializeMetadata(75, length-1, &batch, arena);
    EXPECT_STREQ("gpr_log: Metadata format error: key (2 bytes) and "
            "value (16 bytes) exceed remaining space (17 bytes)",
            Mock::log.c_str());
    batch.Clear();
    arena->Destroy();
}
TEST_F(TestIncoming, deserializeMetadata_useCallout) {
    grpc_error_handle error;
    grpc_core::Arena *arena = grpc_core::Arena::Create(2000);
    HomaIncoming::UniquePtr msg = HomaIncoming::read(&sock, 5, &homaId, &error);
    EXPECT_EQ(GRPC_ERROR_NONE, error);
    ASSERT_TRUE(msg);
    size_t length = msg->addMetadata(75,
            ":path", "value1",
            "name2", "value2", nullptr);
    grpc_metadata_batch batch(arena);
    msg->deserializeMetadata(75, length, &batch, arena);
    Mock::logMetadata("; ", &batch);
    EXPECT_STREQ("metadata :path: value1 (0); metadata name2: value2",
            Mock::log.c_str());
    batch.Clear();
    arena->Destroy();
}
TEST_F(TestIncoming, deserializeMetadata_valueMustBeManaged) {
    grpc_error_handle error;
    grpc_core::Arena *arena = grpc_core::Arena::Create(2000);
    HomaIncoming::UniquePtr msg = HomaIncoming::read(&sock, 5, &homaId, &error);
    EXPECT_EQ(GRPC_ERROR_NONE, error);
    ASSERT_TRUE(msg);
    int destroyCounter = 0;
    msg->destroyCounter = &destroyCounter;
    size_t length = msg->addMetadata(75,
            "name1", "value1",
            "name2", "0123456789abcdefghij", nullptr);
    grpc_metadata_batch batch(arena);
    msg->maxStaticMdLength = 10;
    msg->deserializeMetadata(75, length, &batch, arena);
    Mock::logMetadata("; ", &batch);
    EXPECT_STREQ("metadata name1: value1; "
            "metadata name2: 0123456789abcdefghij",
            Mock::log.c_str());
    msg.reset(nullptr);
    EXPECT_EQ(0, destroyCounter);
    batch.Clear();
    EXPECT_EQ(1, destroyCounter);
    arena->Destroy();
}
TEST_F(TestIncoming, deserializeMetadata_incompleteHeader) {
    grpc_error_handle error;
    grpc_core::Arena *arena = grpc_core::Arena::Create(2000);
    HomaIncoming::UniquePtr msg = HomaIncoming::read(&sock, 5, &homaId, &error);
    EXPECT_EQ(GRPC_ERROR_NONE, error);
    ASSERT_TRUE(msg);
    size_t length = msg->addMetadata(75,
            "name1", "value1",
            "name2", "value2",
            "n3", "abcdefghijklmnop", nullptr);
    grpc_metadata_batch batch(arena);
    msg->deserializeMetadata(75, length+3, &batch, arena);
    EXPECT_SUBSTR("only 3 bytes available", Mock::log.c_str());
    batch.Clear();
    arena->Destroy();
}

TEST_F(TestIncoming, contiguous) {
    grpc_error_handle error;
    HomaIncoming::UniquePtr msg = HomaIncoming::read(&sock, 5, &homaId, &error);
    EXPECT_EQ(GRPC_ERROR_NONE, error);
    msg->control.num_bpages = 3;
    msg->length = 2*HOMA_BPAGE_SIZE + 1000;
    EXPECT_EQ(200U, msg->contiguous(HOMA_BPAGE_SIZE-200));
    EXPECT_EQ(500U, msg->contiguous(2*HOMA_BPAGE_SIZE-500));
    EXPECT_EQ(950U, msg->contiguous(2*HOMA_BPAGE_SIZE+50));
    EXPECT_EQ(0U, msg->contiguous(2*HOMA_BPAGE_SIZE+1000));
}

TEST_F(TestIncoming, getBytes) {
    grpc_error_handle error;
    struct Bytes100 {uint8_t data[100];};
    Bytes100 buffer;
    HomaIncoming::UniquePtr msg = HomaIncoming::read(&sock, 5, &homaId, &error);
    EXPECT_EQ(GRPC_ERROR_NONE, error);
    setBigBuf(msg.get(), 2*HOMA_BPAGE_SIZE + 40000);

    // First extraction is contiguous in a bpage.
    Bytes100 *p = msg->get<Bytes100>(140000, &buffer);
    Mock::logData("; ", p->data, sizeof(p->data));
    EXPECT_STREQ("140000-140099", Mock::log.c_str());
    EXPECT_EQ((140000 + HOMA_BPAGE_SIZE), p->data - bigBuf);

    // Second extraction crosses a bpage boundary.
    p = msg->get<Bytes100>(2*HOMA_BPAGE_SIZE-20, &buffer);
    ASSERT_EQ(&buffer, p);
    Mock::log.clear();
    Mock::logData("; ", p->data, sizeof(p->data));
    EXPECT_STREQ("131052-131151", Mock::log.c_str());

    // Third extraction crosses multiple bpage boundaries.
    struct Bytes90000 {uint8_t data[90000];};
    uint8_t buffer2[100000];
    memset(buffer2, 0, sizeof(buffer2));
    Bytes90000 *p2 = msg->get<Bytes90000>(60000,
            reinterpret_cast<Bytes90000 *>(buffer2));
    ASSERT_EQ(buffer2, p2->data);
    Mock::log.clear();
    Mock::logData("; ", buffer2, 90000);
    EXPECT_STREQ("60000-149999",
            Mock::log.c_str());
    EXPECT_NE(0, *(reinterpret_cast<int32_t *>(buffer2+89996)));
    EXPECT_EQ(0, *(reinterpret_cast<int32_t *>(buffer2+90000)));
}