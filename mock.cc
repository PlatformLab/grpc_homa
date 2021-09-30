#include <linux/types.h>

#include <cstdarg>

#include "homa.h"
#include "mock.h"
#include "util.h"

/* This file provides simplified substitutes for various features, in
 * order to enable better unit testing.
 */

int Mock::errorCode = EIO;
int Mock::homaRecvErrors = 0;
int Mock::homaReplyErrors = 0;
int Mock::homaReplyvErrors = 0;
int Mock::homaSendvErrors = 0;

std::deque<std::vector<uint8_t>> Mock::homaMessages;
std::deque<Wire::Header>         Mock::homaRecvHeaders;
std::deque<ssize_t>              Mock::homaRecvMsgLengths;
std::deque<ssize_t>              Mock::homaRecvReturns;
std::string                      Mock::log;

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
grpc_slice Mock::dataSlice(size_t length, int firstValue)
{
    grpc_slice slice = grpc_slice_malloc(length);
    fillData(GRPC_SLICE_START_PTR(slice), length, firstValue);
    return slice;
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
 * \param stream
 *      Stream containing the data to log.
 */
void Mock::logByteStream(const char *separator,
        grpc_core::ByteStream *byteStream)
{
    size_t dataLeft = byteStream->length();
    grpc_slice slice;
    
    while (dataLeft > 0) {
        if (!byteStream->Next(dataLeft, nullptr)) {
            logPrintf(";", "byteStream->Next failed");
            return;
        }
        if (byteStream->Pull(&slice) != GRPC_ERROR_NONE) {
            logPrintf(";", "byteStream->Pull failed");
            return;
        }
        logData(separator, GRPC_SLICE_START_PTR(slice),
                GRPC_SLICE_LENGTH(slice));
        dataLeft -= GRPC_SLICE_LENGTH(slice);
        grpc_slice_unref(slice);
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
void Mock::logData(const char *separator, void *data, int length)
{
    int i, rangeStart, expectedNext;
    uint8_t* p = static_cast<uint8_t *>(data);
    if (length == 0) {
        logPrintf(separator, "empty block");
        return;
    }
    if (length >= 4) {
        rangeStart = *reinterpret_cast<int32_t *>(p);
    } else {
        rangeStart = 0;
    }
    expectedNext = rangeStart;
    for (i = 0; i <= length-4; i += 4) {
        int current = *reinterpret_cast<int32_t *>(p + i);
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
    for (grpc_linked_mdelem* md = batch->list.head; md != nullptr;
            md = md->next) {
        const grpc_slice& key = GRPC_MDKEY(md->md);
        const grpc_slice& value = GRPC_MDVALUE(md->md);
        logPrintf(separator, "metadata %.*s: %.*s (%d)",
                GRPC_SLICE_LENGTH(key), GRPC_SLICE_START_PTR(key),
                GRPC_SLICE_LENGTH(value), GRPC_SLICE_START_PTR(value),
                GRPC_BATCH_INDEX_OF(key));
    }
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
        const char *value, grpc_core::Arena *arena)
{
    // This must be byte-compatible with grpc_mdelem_data. Need this
    // hack in order to avoid high overheads (e.g. extra memory allocation)
    // of the builtin mechanisms for creating metadata.
    struct ElemData {
        grpc_slice key;
        grpc_slice value;
        ElemData() : key(), value() {}
    };

    ElemData *elemData = arena->New<ElemData>();
    grpc_linked_mdelem* lm = arena->New<grpc_linked_mdelem>();
    elemData->key = grpc_slice_from_static_string(key);
    elemData->value = grpc_slice_from_static_string(value);
    lm->md = GRPC_MAKE_MDELEM(elemData, GRPC_MDELEM_STORAGE_EXTERNAL);
    grpc_error_handle error = grpc_metadata_batch_link_tail(batch, lm);
    if (error != GRPC_ERROR_NONE) {
        logPrintf("; ", "grpc_metadata_batch_link_tail returned error: %s",
                grpc_error_string(error));
    }
}

/**
 * Invoked at the start of each unit test to reset all mocking information.
 */
void Mock::setUp(void)
{
    grpc_init();
    gpr_set_log_function(gprLog);
    gpr_set_log_verbosity(GPR_LOG_SEVERITY_ERROR);
    
    errorCode = EIO;
    homaRecvErrors = 0;
    homaReplyErrors = 0;
    homaReplyvErrors = 0;
    homaSendvErrors = 0;
    
    homaMessages.clear();
    
    homaRecvHeaders.clear();
    homaRecvMsgLengths.clear();
    homaRecvReturns.clear();
    log.clear();
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

ssize_t homa_recv(int sockfd, void *buf, size_t len, int flags,
        struct sockaddr *srcAddr, size_t *addrlen, uint64_t *id,
        size_t *msglen)
{
    Wire::Header *h = static_cast<Wire::Header *>(buf);
    if (Mock::checkError(&Mock::homaRecvErrors)) {
        errno = Mock::errorCode;
        return -1;
    }
    *id = 333;
    if (Mock::homaRecvHeaders.empty()) {
        new (buf) Wire::Header(44, 0, 10, 20, 1000);
    } else {
        *(reinterpret_cast<Wire::Header *>(buf)) = Mock::homaRecvHeaders.front();
        Mock::homaRecvHeaders.pop_front();
    }
    size_t length;
    if (Mock::homaRecvMsgLengths.empty()) {
        length = sizeof(*h) + ntohl(h->initMdBytes) + ntohl(h->messageBytes)
            + ntohl(h->trailMdBytes);
    } else {
        length = Mock::homaRecvMsgLengths.front();
        Mock::homaRecvMsgLengths.pop_front();
    }
    if (msglen) {
        *msglen = length;
    }
    if (Mock::homaRecvReturns.empty()) {
        return length;
    }
    ssize_t result = Mock::homaRecvReturns.front();
    Mock::homaRecvReturns.pop_front();
    return result;
}

ssize_t homa_reply(int sockfd, const void *buffer, size_t length,
        const struct sockaddr *dest_addr, size_t addrlen, uint64_t id)
{    
    Mock::homaMessages.emplace_back(length);
    memcpy(Mock::homaMessages.back().data(), buffer, length);
    
    if (Mock::checkError(&Mock::homaReplyErrors)) {
        errno = Mock::errorCode;
        return -1;
    }
    return length;
}

ssize_t homa_replyv(int sockfd, const struct iovec *iov, int iovcnt,
        const struct sockaddr *dest_addr, size_t addrlen, uint64_t id)
{
    size_t totalLength = 0;
    for (int i = 0; i < iovcnt; i++) {
        totalLength += iov[i].iov_len;
    }
    Mock::logPrintf("; ", "homa_replyv: %d iovecs, %lu bytes", iovcnt,
            totalLength);
    
    Mock::homaMessages.emplace_back();
    Mock::homaMessages.back().resize(totalLength);
    uint8_t *dst = Mock::homaMessages.back().data();
    for (int i = 0; i < iovcnt; i++) {
        memcpy(dst, iov[i].iov_base, iov[i].iov_len);
        dst += iov[i].iov_len;
    }
    
    if (Mock::checkError(&Mock::homaReplyvErrors)) {
        errno = Mock::errorCode;
        return -1;
    }
    return totalLength;
}

int homa_sendv(int sockfd, const struct iovec *iov, int iovcnt,
        const struct sockaddr *dest_addr, size_t addrlen, uint64_t *id)
{
    size_t totalLength = 0;
    for (int i = 0; i < iovcnt; i++) {
        totalLength += iov[i].iov_len;
    }
    Mock::logPrintf("; ", "homa_sendv: %d iovecs, %lu bytes", iovcnt,
            totalLength);
    
    Mock::homaMessages.emplace_back();
    Mock::homaMessages.back().resize(totalLength);
    uint8_t *dst = Mock::homaMessages.back().data();
    for (int i = 0; i < iovcnt; i++) {
        memcpy(dst, iov[i].iov_base, iov[i].iov_len);
        dst += iov[i].iov_len;
    }
    
    if (Mock::checkError(&Mock::homaSendvErrors)) {
        errno = Mock::errorCode;
        return -1;
    }
    *id = 123;
    return totalLength;
}