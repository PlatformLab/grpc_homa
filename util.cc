#include "util.h"

/**
 * Generate log messages describing a batch of metadata.
 * \param mdBatch
 *      Metadata for which to generate log messages.
 * \param isClient
 *      True means we're running client code, false means server.
 * \param isInitial
 *      True means mdBatch refers to initial metadata, valse means trailing.
 */
void logMetadata(const grpc_metadata_batch* mdBatch, bool isClient,
        bool isInitial)
{
    if (!mdBatch->list.head) {
        gpr_log(GPR_INFO, "%s:%s: No metadata", isInitial ? "HDR" : "TRL",
                isClient ? "CLI" : "SVR");
    }
    for (grpc_linked_mdelem* md = mdBatch->list.head; md != nullptr;
            md = md->next) {
        char* key = grpc_slice_to_c_string(GRPC_MDKEY(md->md));
        char* value = grpc_slice_to_c_string(GRPC_MDVALUE(md->md));
        gpr_log(GPR_INFO, "%s:%s: %s: %s", isInitial ? "HDR" : "TRL",
                isClient ? "CLI" : "SVR", key, value);
        gpr_free(key);
        gpr_free(value);
    }
}