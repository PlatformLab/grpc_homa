#include "homa_incoming.h"
#include "mock.h"
#include "util.h"

// This file contains unit tests for homa_incoming.cc and homa_incoming.h.

class TestIncoming : public ::testing::Test {
public:   
    uint64_t homaId;
    TestIncoming()
        : homaId(0)
    {
        Mock::setUp();
    }
};

TEST_F(TestIncoming, read_basics) {
    grpc_error_handle error;
    HomaIncoming::UniquePtr msg = HomaIncoming::read(2, 5, &homaId, &error);
    ASSERT_TRUE(msg);
    EXPECT_EQ(44U, msg->streamId.id);
    EXPECT_EQ(1000U, msg->messageLength);
    EXPECT_EQ(1051U, msg->baseLength);
    EXPECT_EQ(GRPC_ERROR_NONE, error);
}
TEST_F(TestIncoming, read_noMessageAvailable) {
    grpc_error_handle error;
    Mock::homaRecvErrors = 1;
    Mock::errorCode = EAGAIN;
    HomaIncoming::UniquePtr msg = HomaIncoming::read(2, 5, &homaId, &error);
    EXPECT_EQ(GRPC_ERROR_NONE, error);
    EXPECT_EQ(nullptr, msg.get());
    EXPECT_STREQ("", Mock::log.c_str());
}
TEST_F(TestIncoming, read_firstHomaRecvFails) {
    grpc_error_handle error;
    Mock::homaRecvErrors = 1;
    gpr_set_log_verbosity(GPR_LOG_SEVERITY_DEBUG);
    HomaIncoming::UniquePtr msg = HomaIncoming::read(2, 5, &homaId, &error);
    EXPECT_NE(GRPC_ERROR_NONE, error);
    EXPECT_EQ(nullptr, msg.get());
    EXPECT_SUBSTR("gpr_log: Error in homa_recv",
            Mock::log.c_str());
    EXPECT_SUBSTR("os_error", grpc_error_string(error));
}
TEST_F(TestIncoming, read_firsthomaRecvTooShort) {
    grpc_error_handle error;
    Mock::homaRecvMsgLengths.push_back(4);
    HomaIncoming::UniquePtr msg = HomaIncoming::read(2, 5, &homaId, &error);
    EXPECT_NE(GRPC_ERROR_NONE, error);
    EXPECT_EQ(nullptr, msg.get());
    EXPECT_SUBSTR("gpr_log: Homa message contained only 4 bytes",
            Mock::log.c_str());
    EXPECT_SUBSTR("Incoming Homa message too short for header",
            grpc_error_string(error));
}
TEST_F(TestIncoming, read_discardStreamingResponses) {
    Mock::homaRecvHeaders.emplace_back(1, 2);
    Mock::homaRecvHeaders.emplace_back(1, 3);
    Mock::homaRecvHeaders.emplace_back(2, 1);
    Mock::homaRecvHeaders[0].flags |= Wire::Header::emptyResponse;
    Mock::homaRecvHeaders[1].flags |= Wire::Header::emptyResponse;
    grpc_error_handle error;
    HomaIncoming::UniquePtr msg = HomaIncoming::read(2, 5, &homaId, &error);
    EXPECT_EQ(GRPC_ERROR_NONE, error);
    EXPECT_EQ(2U, msg->streamId.id);
    EXPECT_EQ(1, msg->sequence);
    EXPECT_EQ(0U, Mock::homaRecvHeaders.size());
}
TEST_F(TestIncoming, read_lengthsInconsistent) {
    grpc_error_handle error;
    Mock::homaRecvMsgLengths.push_back(1000);
    HomaIncoming::UniquePtr msg = HomaIncoming::read(2, 5, &homaId, &error);
    EXPECT_NE(GRPC_ERROR_NONE, error);
    EXPECT_EQ(nullptr, msg.get());
    EXPECT_SUBSTR("gpr_log: Bad message length 1000",
            Mock::log.c_str());
    EXPECT_SUBSTR("Incoming Homa message length doesn't match header",
            grpc_error_string(error));
}
TEST_F(TestIncoming, read_tailhomaRecvFails) {
    grpc_error_handle error;
    Mock::homaRecvErrors = 2;
    Mock::homaRecvReturns.push_back(500);
    HomaIncoming::UniquePtr msg = HomaIncoming::read(2, 5, &homaId, &error);
    EXPECT_NE(GRPC_ERROR_NONE, error);
    EXPECT_EQ(nullptr, msg.get());
    EXPECT_SUBSTR("gpr_log: Error in homa_recv for tail of id 333:",
            Mock::log.c_str());
    EXPECT_SUBSTR("os_error", grpc_error_string(error));
}
TEST_F(TestIncoming, read_tailHasWrongLength) {
    grpc_error_handle error;
    Mock::homaRecvReturns.push_back(500);
    Mock::homaRecvReturns.push_back(500);
    HomaIncoming::UniquePtr msg = HomaIncoming::read(2, 5, &homaId, &error);
    EXPECT_NE(GRPC_ERROR_NONE, error);
    EXPECT_EQ(nullptr, msg.get());
    EXPECT_SUBSTR("gpr_log: Tail of Homa message has wrong length",
            Mock::log.c_str());
    EXPECT_SUBSTR("Tail of Homa message length has wrong length",
            grpc_error_string(error));
}
TEST_F(TestIncoming, read_tailOK) {
    grpc_error_handle error;
    Mock::homaRecvReturns.push_back(500);
    Mock::homaRecvReturns.push_back(551);
    HomaIncoming::UniquePtr msg = HomaIncoming::read(2, 5, &homaId, &error);
    EXPECT_EQ(GRPC_ERROR_NONE, error);
    ASSERT_TRUE(msg);
    EXPECT_LT(100U, msg->tail.size());
}

TEST_F(TestIncoming, copyOut) {
    grpc_error_handle error;
    int destroyCounter = 0;
    HomaIncoming::UniquePtr msg = HomaIncoming::read(2, 5, &homaId, &error);
    EXPECT_EQ(GRPC_ERROR_NONE, error);
    msg->destroyCounter = &destroyCounter;
    msg->baseLength = 500;
    msg->tail.resize(1000);
    fillData(msg->initialPayload, 500, 0);
    fillData(msg->tail.data(), 1000, 1000);
    
    // First block is in the static part of the message.
    char buffer[40];
    msg->copyOut(buffer, 460, sizeof(buffer));
    Mock::logData("; ", buffer, sizeof(buffer));
    EXPECT_STREQ("460-499", Mock::log.c_str());
    
    // Second slice is entirely in the tail of the message.
    Mock::log.clear();
    msg->copyOut(buffer, 500, sizeof(buffer));
    Mock::logData("; ", buffer, sizeof(buffer));
    EXPECT_STREQ("1000-1039", Mock::log.c_str());
    
    // Third slice straddles the boundary.
    Mock::log.clear();
    msg->copyOut(buffer, 484, sizeof(buffer));
    Mock::logData("; ", buffer, sizeof(buffer));
    EXPECT_STREQ("484-499 1000-1023", Mock::log.c_str());\
}

TEST_F(TestIncoming, getStaticSlice) {
    grpc_error_handle error;
    grpc_core::Arena *arena = grpc_core::Arena::Create(2000);
    HomaIncoming::UniquePtr msg = HomaIncoming::read(2, 5, &homaId, &error);
    EXPECT_EQ(GRPC_ERROR_NONE, error);
    msg->baseLength = 500;
    fillData(msg->initialPayload, 500, 0);
    
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
    HomaIncoming::UniquePtr msg = HomaIncoming::read(2, 5, &homaId, &error);
    EXPECT_EQ(GRPC_ERROR_NONE, error);
    msg->destroyCounter = &destroyCounter;
    msg->baseLength = 500;
    msg->tail.resize(1000);
    fillData(msg->initialPayload, 500, 0);
    fillData(msg->tail.data(), 1000, 1000);
    
    // First slice is in the static part of the message.
    grpc_slice slice1 = msg->getSlice(440, 60);
    Mock::logData("; ", GRPC_SLICE_START_PTR(slice1), GRPC_SLICE_LENGTH(slice1));
    EXPECT_STREQ("440-499", Mock::log.c_str());
    
    // Second slice is entirely in the tail of the message.
    Mock::log.clear();
    grpc_slice slice2 = msg->getSlice(500, 100);
    Mock::logData("; ", GRPC_SLICE_START_PTR(slice2), GRPC_SLICE_LENGTH(slice2));
    EXPECT_STREQ("1000-1099", Mock::log.c_str());
    
    // Third slice straddles the boundary.
    Mock::log.clear();
    grpc_slice slice3 = msg->getSlice(420,200);
    Mock::logData("; ", GRPC_SLICE_START_PTR(slice3), GRPC_SLICE_LENGTH(slice3));
    EXPECT_STREQ("420-499 1000-1119", Mock::log.c_str());
    
    // Now make sure that the reference counting worked correctly.
    EXPECT_EQ(0, destroyCounter);
    msg.reset(nullptr);
    EXPECT_EQ(0, destroyCounter);
    grpc_slice_unref(slice3);
    EXPECT_EQ(0, destroyCounter);
    grpc_slice_unref(slice2);
    EXPECT_EQ(0, destroyCounter);
    grpc_slice_unref(slice1);
    EXPECT_EQ(1, destroyCounter);
}

TEST_F(TestIncoming, deserializeMetadata_basics) {
    grpc_error_handle error;
    grpc_core::Arena *arena = grpc_core::Arena::Create(2000);
    HomaIncoming::UniquePtr msg = HomaIncoming::read(2, 5, &homaId, &error);
    EXPECT_EQ(GRPC_ERROR_NONE, error);
    ASSERT_TRUE(msg);
    int destroyCounter = 0;
    msg->destroyCounter = &destroyCounter;
    size_t length = msg->addMetadata(75, 100,
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
    HomaIncoming::UniquePtr msg = HomaIncoming::read(2, 5, &homaId, &error);
    EXPECT_EQ(GRPC_ERROR_NONE, error);
    ASSERT_TRUE(msg);
    size_t length = msg->addMetadata(75, 100,
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
    HomaIncoming::UniquePtr msg = HomaIncoming::read(2, 5, &homaId, &error);
    EXPECT_EQ(GRPC_ERROR_NONE, error);
    ASSERT_TRUE(msg);
    size_t length = msg->addMetadata(75, 1000,
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
    HomaIncoming::UniquePtr msg = HomaIncoming::read(2, 5, &homaId, &error);
    EXPECT_EQ(GRPC_ERROR_NONE, error);
    ASSERT_TRUE(msg);
    int destroyCounter = 0;
    msg->destroyCounter = &destroyCounter;
    size_t length = msg->addMetadata(75, 1000,
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
    HomaIncoming::UniquePtr msg = HomaIncoming::read(2, 5, &homaId, &error);
    EXPECT_EQ(GRPC_ERROR_NONE, error);
    ASSERT_TRUE(msg);
    size_t length = msg->addMetadata(75, 100,
            "name1", "value1",
            "name2", "value2",
            "n3", "abcdefghijklmnop", nullptr);
    grpc_metadata_batch batch(arena);
    msg->deserializeMetadata(75, length+3, &batch, arena);
    EXPECT_SUBSTR("only 3 bytes available", Mock::log.c_str());
    batch.Clear();
    arena->Destroy();
}

TEST_F(TestIncoming, getBytes) {
    grpc_error_handle error;
    struct Bytes16 {uint8_t data[16];};
    Bytes16 buffer;
    HomaIncoming::UniquePtr msg = HomaIncoming::read(2, 5, &homaId, &error);
    EXPECT_EQ(GRPC_ERROR_NONE, error);
    msg->baseLength = 500;
    msg->tail.resize(1000);
    fillData(msg->initialPayload, 500, 0);
    fillData(msg->tail.data(), 1000, 1000);
    
    // First extraction fits in initial data.
    Bytes16 *p = msg->getBytes<Bytes16>(484, &buffer);
    Mock::logData("; ", p->data, 16);
    EXPECT_STREQ("484-499", Mock::log.c_str());
    
    // Second extraction straddles the initial data and the tail.
    Mock::log.clear();
    p = msg->getBytes<Bytes16>(496, &buffer);
    Mock::logData("; ", p->data, 16);
    EXPECT_STREQ("496-499 1000-1011", Mock::log.c_str());
    
    // Third extraction is entirely in the tail.
    Mock::log.clear();
    p = msg->getBytes<Bytes16>(500, &buffer);
    Mock::logData("; ", p->data, 16);
    EXPECT_STREQ("1000-1015", Mock::log.c_str());
}