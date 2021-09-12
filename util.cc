#include "util.h"

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
    if (!mdBatch->list.head) {
        gpr_log(GPR_INFO, "%s: metadata empty", info);
    }
    for (grpc_linked_mdelem* md = mdBatch->list.head; md != nullptr;
            md = md->next) {
        char* key = grpc_slice_to_c_string(GRPC_MDKEY(md->md));
        char* value = grpc_slice_to_c_string(GRPC_MDVALUE(md->md));
        gpr_log(GPR_INFO, "%s: %s: %s (index %d)", info, key, value,
                GRPC_BATCH_INDEX_OF(GRPC_MDKEY(md->md)));
        gpr_free(key);
        gpr_free(value);
    }
}