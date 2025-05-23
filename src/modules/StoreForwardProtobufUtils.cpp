#include "StoreForwardProtobufUtils.h"
#include <string.h>

/**
 * Copies binary data into a protobuf bytes field
 * Generic template implementation that works with any type of protobuf bytes array
 */
template <typename PB_BYTES_ARRAY_TYPE> bool copyToProtobufBytes(PB_BYTES_ARRAY_TYPE &bytes, const uint8_t *src, size_t size)
{
    if (!src || size > 255) {
        return false;
    }

    // Set the size
    bytes.size = size;

    // Copy the data
    memcpy(bytes.bytes, src, size);

    return true;
}

// Explicit template instantiation for the specific type used in StoreForwardModule
template bool
copyToProtobufBytes<meshtastic_StoreAndForward_TextPayload_bytes_t>(meshtastic_StoreAndForward_TextPayload_bytes_t &bytes,
                                                                    const uint8_t *src, size_t size);
