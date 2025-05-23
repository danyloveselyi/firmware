#pragma once

#include "mesh/generated/meshtastic/storeforward.pb.h"
#include <pb.h>

/**
 * Utility functions for working with protocol buffer messages in Store & Forward
 */

/**
 * Copies binary data to protobuf bytes field
 *
 * @param bytes Reference to the destination bytes array
 * @param src Source buffer
 * @param size Number of bytes to copy
 * @return true if successful, false otherwise
 */
template <typename PB_BYTES_ARRAY_TYPE> bool copyToProtobufBytes(PB_BYTES_ARRAY_TYPE &bytes, const uint8_t *src, size_t size);

// Declare but don't define the specialization - definition is in .cpp file
extern template bool
copyToProtobufBytes<meshtastic_StoreAndForward_TextPayload_bytes_t>(meshtastic_StoreAndForward_TextPayload_bytes_t &bytes,
                                                                    const uint8_t *src, size_t size);
