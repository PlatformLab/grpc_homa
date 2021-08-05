#include "util.h"

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