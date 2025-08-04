#include <linux/types.h>

#include <cstdarg>

#include "homa.h"
#include "mock.h"
#include "stream_id.h"
#include "util.h"

/* This file provides simplified substitutes for various features, in
 * order to enable better unit testing.
 */

int Mock::errorCode = EIO;
int Mock::recvmsgErrors = 0;
int Mock::sendmsgErrors = 0;

std::deque<std::vector<uint8_t>> Mock::homaMessages;
std::deque<Wire::Header>         Mock::recvmsgHeaders;
std::deque<ssize_t>              Mock::recvmsgLengths;
std::deque<ssize_t>              Mock::recvmsgReturns;
std::string                      Mock::log;

// Used as receive buffer space by recvmsg.
static uint8_t bufStorage[10000];
uint8_t *Mock::bufRegion = bufStorage;

// Counts the number of buffers that were returned to Homa in
// recvmsg calls.
int Mock::buffersReturned = 0;

/**
 * Determines whether a method should simulate an error return.
 * \param errorMask
 *      Address of a variable containing a bit mask, indicating which of the
 *      next calls should result in errors. The variable is modified by
 *      this function.
 * \return
 *      Zero means the function should behave normally; 1 means return
 *      an error.
 */
int Mock::checkError(int *errorMask)
{
	int result = *errorMask & 1;
	*errorMask = *errorMask >> 1;
	return result;
}

/**
 * Returns a slice containing well-known data.
 * \param length
 *      Total number of bytes of data in the new slice
 * \param firstValue
 *      Used as a parameter to fillData to determine the contents of the slice.
 * \return
 *      The new slice (now owned by caller).
 */
grpc_core::Slice Mock::dataSlice(size_t length, int firstValue)
{
    grpc_slice slice = grpc_slice_malloc(length);
    fillData(GRPC_SLICE_START_PTR(slice), length, firstValue);
    return grpc_core::Slice(slice);
}

/**
 * This method captures calls to gpr_log and logs the information in
 * the Mock log.
 * \param args
 *      Information from the original gpr_log call.
 */
void Mock::gprLog(gpr_log_func_args* args)
{
    logPrintf("; ", "gpr_log: %s", args->message);
}

/**
 * Log information about the data in a byte stream.
 * \param separator
 *      Initial separator in the log.
 * \param sliceBuffer
 *      SliceBuffer containing the data to log.
 */
void Mock::logSliceBuffer(const char *separator,
        const grpc_core::SliceBuffer *sliceBuffer)
{
    for (size_t i = 0; i < sliceBuffer->Count(); i++) {
        const grpc_core::Slice &slice = (*sliceBuffer)[i];
        logData(separator, slice.data(), slice.length());
    }
}

/**
 * Log information that describes a block of data previously encoded
 * with fillData.
 * \param separator
 *      Initial separator in the log.
 * \param data
 *      Address of first byte of data. Should have been encoded with
 *      fillData.
 * \param length
 *      Total amount of data, in bytes.
 */
void Mock::logData(const char *separator, const void *data, int length)
{
    int i, rangeStart, expectedNext;
    const uint8_t* p = static_cast<const uint8_t *>(data);
    if (length == 0) {
        logPrintf(separator, "empty block");
        return;
    }
    if (length >= 4) {
        rangeStart = *reinterpret_cast<const int32_t *>(p);
    } else {
        rangeStart = 0;
    }
    expectedNext = rangeStart;
    for (i = 0; i <= length-4; i += 4) {
        int current = *reinterpret_cast<const int32_t *>(p + i);
        if (current != expectedNext) {
            logPrintf(separator, "%d-%d", rangeStart, expectedNext-1);
            separator = " ";
            rangeStart = current;
        }
        expectedNext = current+4;
    }
    logPrintf(separator, "%d-%d", rangeStart, expectedNext-1);
    separator = " ";

    for ( ; i < length; i += 1) {
        logPrintf(separator, "0x%x", p[i]);
        separator = " ";
    }
}

/**
 * Print information about a batch of metadata to Mock::log.
 * \param separator
 *      Log separator string (before each metadata entry).
 * \param batch
 *      Metadata to print.
 */
void Mock::logMetadata(const char *separator, const grpc_metadata_batch *batch)
{
    batch->Log([separator] (absl::string_view key, absl::string_view value) {
        Mock::logPrintf(separator, "metadata %.*s: %.*s", key.length(),
                key.data(), value.length(), value.data());
    });
}

/**
 * Append information to the test log.
 * \param separator
 *      If non-NULL, and if the log is non-empty, this string is added to
 *      the log before the new message.
 * \param format
 *      Standard printf-style format string.
 * \param ap
 *      Additional arguments as required by @format.
 */
void Mock::logPrintf(const char *separator, const char* format, ...)
{
	va_list ap;
	va_start(ap, format);

	if (!log.empty() && (separator != NULL))
		log.append(separator);

	// We're not really sure how big of a buffer will be necessary.
	// Try 1K, if not the return value will tell us how much is necessary.
	int bufSize = 1024;
	while (true) {
		char buf[bufSize];
		// vsnprintf trashes the va_list, so copy it first
		va_list aq;
		__va_copy(aq, ap);
		int length = vsnprintf(buf, bufSize, format, aq);
		if (length < bufSize) {
			log.append(buf, length);
			break;
		}
		bufSize = length + 1;
	}
	va_end(ap);
}

/**
 * Add a new element to an existing batch of metadata.
 * \param batch
 *      The new element will be added to this batch.
 * \param key
 *       Key for the new element
 * \param value
 *       Value for the new element
 * \param arena
 *       Storage for the new element is allocated from here.
 */
void Mock::metadataBatchAppend(grpc_metadata_batch* batch, const char *key,
        const char *value)
{
    auto md = grpc_metadata_batch::Parse(absl::string_view(key),
            grpc_core::Slice::FromStaticBuffer(value, strlen(value)),
            false, strlen(key) + strlen(value),
            [&key, &value] (absl::string_view error,
                    const grpc_core::Slice& v) -> void {
                int msgLength = error.length();
                logPrintf("Mock::metadataBatchAppend couldn't parse value '%s' "
                        "for metadata key '%s': %.*s",
                        value, key, msgLength, error.data());
            });
    batch->Set(std::move(md));
}

/**
 * Invoked at the start of each unit test to reset all mocking information.
 */
void Mock::setUp(void)
{
    gpr_set_log_function(gprLog);
    gpr_set_log_verbosity(GPR_LOG_SEVERITY_ERROR);
    grpc_init();

    errorCode = EIO;
    recvmsgErrors = 0;
    sendmsgErrors = 0;

    buffersReturned = 0;

    homaMessages.clear();

    recvmsgHeaders.clear();
    recvmsgLengths.clear();
    recvmsgReturns.clear();
    log.clear();

    bufRegion = bufStorage;
}

/**
 * Used in EXPECT_SUBSTR to fail a gtest test case if a given string
 * doesn't contain a given substring.
 * \param s
 *      Test output.
 * \param substring
 *      Substring expected to appear somewhere in s.
 * \return
 *      A value that can be tested with EXPECT_TRUE.
 */
::testing::AssertionResult
Mock::substr(const std::string& s, const std::string& substring)
{
    if (s.find(substring) == s.npos) {
        char buffer[1000];
        snprintf(buffer, sizeof(buffer), "Substring '%s' not present in '%s'",
                substring.c_str(), s.c_str());
        std::string message(buffer);
        return ::testing::AssertionFailure() << message;
    }
    return ::testing::AssertionSuccess();
}

ssize_t recvmsg(int fd, struct msghdr *msg, int flags)
{
    struct homa_recvmsg_args *recvArgs =
            static_cast<homa_recvmsg_args *>(msg->msg_control);
    Mock::buffersReturned += reinterpret_cast<homa_recvmsg_args *>
            (msg->msg_control)->num_bpages;
    if (Mock::checkError(&Mock::recvmsgErrors)) {
        errno = Mock::errorCode;
        return -1;
    }
    recvArgs->id = 333;
    recvArgs->completion_cookie = 44444;
    recvArgs->num_bpages = 1;
    recvArgs->bpage_offsets[0] = 2000;
    Wire::Header *h = reinterpret_cast<Wire::Header *>(
            Mock::bufRegion + recvArgs->bpage_offsets[0]);

    if (Mock::recvmsgHeaders.empty()) {
        new (h) Wire::Header(44, 0, 10, 20, 1000);
    } else {
        *(reinterpret_cast<Wire::Header *>(h)) = Mock::recvmsgHeaders.front();
        Mock::recvmsgHeaders.pop_front();
    }
    size_t length;
    if (Mock::recvmsgLengths.empty()) {
        length = sizeof(Wire::Header) + ntohl(h->initMdBytes)
                + ntohl(h->messageBytes) + ntohl(h->trailMdBytes);
    } else {
        length = Mock::recvmsgLengths.front();
        Mock::recvmsgLengths.pop_front();
    }
    if (Mock::recvmsgReturns.empty()) {
        return length;
    }
    ssize_t result = Mock::recvmsgReturns.front();
    Mock::recvmsgReturns.pop_front();
    return result;
}

ssize_t sendmsg(int fd, const struct msghdr *msg, int flags)
{
    size_t totalLength = 0;
    struct homa_sendmsg_args *args =
            static_cast<homa_sendmsg_args *>(msg->msg_control);

    for (size_t i = 0; i < msg->msg_iovlen; i++) {
        totalLength += msg->msg_iov[i].iov_len;
    }
    if (args->id != 0) {
        Mock::logPrintf("; ", "sendmsg reply to id %lu: %d iovecs, %lu bytes",
                args->id, msg->msg_iovlen, totalLength);
    } else {
        Mock::logPrintf("; ", "sendmsg request: %d iovecs, %lu bytes",
                msg->msg_iovlen, totalLength);
    }

    Mock::homaMessages.emplace_back();
    Mock::homaMessages.back().resize(totalLength);
    uint8_t *dst = Mock::homaMessages.back().data();
    for (size_t i = 0; i < msg->msg_iovlen; i++) {
        memcpy(dst, msg->msg_iov[i].iov_base, msg->msg_iov[i].iov_len);
        dst += msg->msg_iov[i].iov_len;
    }

    if (Mock::checkError(&Mock::sendmsgErrors)) {
        errno = Mock::errorCode;
        return -1;
    }
    if (args->id == 0) {
        args->id = 123;
    }
    return totalLength;
}
