#include "homa.h"

#include "util.h"

/**
 * Returns a human-readable string containing the bpage indexes in a
 * homa_recvmsg_args structure.
 * \param recvArgs
 *      Structure whose buffers entries should be returned.
 */
std::string bpagesToString(struct homa_recvmsg_args *recvArgs)
{
    std::string result;
    for (uint32_t i = 0; i < recvArgs->num_bpages; i++) {
        char buffer[100];
        if (!result.empty()) {
            result.append(" ");
        }
        snprintf(buffer, sizeof(buffer), "%u", recvArgs->bpage_offsets[i]);
        result.append(buffer);
    }
    return result;
}

/**
 * Fill in a block of memory with predictable values that can be checked
 * later by Mock::log_data.
 * \param data
 *      Address of first byte of data.
 * \param length
 *      Total amount of data, in bytes.
 * \param firstValue
 *      Value to store in first 4 bytes of data. Each successive 4 bytes
 *      of data will have a value 4 greater than the previous.
 */
void fillData(void *data, int length, int firstValue)
{
	int i;
    uint8_t *p = static_cast<uint8_t *>(data);
	for (i = 0; i <= length-4; i += 4) {
		*reinterpret_cast<int32_t *>(p + i) = firstValue + i;
	}

	/* Fill in extra bytes with a special value. */
	for ( ; i < length; i += 1) {
		p[i] = 0xaa;
	}
}

/**
 * Generate log messages describing a batch of metadata.
 * \param mdBatch
 *      Metadata for which to generate log messages.
 * \param info
 *      Additional inforation about the nature of this metadata (included
 *      in each log message).
 */
void logMetadata(const grpc_metadata_batch* mdBatch, const char *info)
{
    if (mdBatch->empty()) {
        gpr_log(GPR_INFO, "%s: metadata empty", info);
    }
    mdBatch->Log([info] (absl::string_view key, absl::string_view value) {
        gpr_log(GPR_INFO, "%s: %.*s -> %.*s", info,
                static_cast<int>(key.length()), key.data(),
                static_cast<int>(value.length()), value.data());
    });
}

/**
 * Generate a string using printf-style arguments.
 * \param format
 *      Standard printf-style format string.
 * \param ...
 *      Values as needed to plug into the formula.
 */
std::string stringPrintf(const char* format, ...)
{
    std::string result;
	va_list ap;
	va_start(ap, format);

	// We're not really sure how big of a buffer will be necessary.
	// Try 1K, if not the return value will tell us how much is necessary.
	int buf_size = 1024;
	while (true) {
		char buf[buf_size];
		// vsnprintf trashes the va_list, so copy it first
		va_list aq;
		__va_copy(aq, ap);
		int length = vsnprintf(buf, buf_size, format, aq);
		assert(length >= 0); // old glibc versions returned -1
		if (length < buf_size) {
			result.append(buf, length);
			break;
		}
		buf_size = length + 1;
	}
	va_end(ap);
    return result;
}

/**
 * Generate a string describing all the useful information in a gRPC
 * Status value.
 */
std::string stringForStatus(grpc::Status *status)
{
    std::string message = status->error_message();
    if (message.empty()) {
        return symbolForCode(status->error_code());
    }
    return stringPrintf("%s (%s)", symbolForCode(status->error_code()),
            message.c_str());
}

/**
 * Return a printable string corresponding to a gRPC status code.
 */
const char *symbolForCode(grpc::StatusCode code)
{
    static char buffer[100];
    switch (code) {
        case grpc::OK:
            return "OK";
        case grpc::CANCELLED:
            return "CANCELLED";
        case grpc::UNKNOWN:
            return "UNKNOWN";
        case grpc::INVALID_ARGUMENT:
            return "INVALID_ARGUMENT";
        case grpc::DEADLINE_EXCEEDED:
            return "DEADLINE_EXCEEDED";
        case grpc::NOT_FOUND:
            return "NOT_FOUND";
        case grpc::ALREADY_EXISTS:
            return "ALREADY_EXISTS";
        case grpc::PERMISSION_DENIED:
            return "PERMISSION_DENIED";
        case grpc::RESOURCE_EXHAUSTED:
            return "RESOURCE_EXHAUSTED";
        case grpc::FAILED_PRECONDITION:
            return "FAILED_PRECONDITION";
        case grpc::ABORTED:
            return "ABORTED";
        case grpc::OUT_OF_RANGE:
            return "OUT_OF_RANGE";
        case grpc::UNIMPLEMENTED:
            return "UNIMPLEMENTED";
        case grpc::INTERNAL:
            return "INTERNAL";
        case grpc::UNAVAILABLE:
            return "UNAVAILABLE";
        case grpc::DATA_LOSS:
            return "DATA_LOSS";
        case grpc::UNAUTHENTICATED:
            return "UNAUTHENTICATED";
        default:
            snprintf(buffer, sizeof(buffer), "Unknown status %d", code);
            return buffer;
    }
}
