#pragma once

// Ensure we don't conflict with any real FS.h that might be included later
// This must be included BEFORE including any Arduino header files
#ifndef STUB_FS_LOADED
#define STUB_FS_LOADED

#include <cstddef> // size_t
#include <cstdint> // uint8_t
#include <utility> // std::move

// The real FS implementations might already define these, so we need a way to detect if we should
// define our own version or use existing ones
#ifndef USE_REAL_FS_CLASSES

#endif // USE_REAL_FS_CLASSES

// Define file open modes (not needed by our stub, but might be used elsewhere)
#ifndef FILE_O_READ
#define FILE_O_READ "r"
#define FILE_O_WRITE "w"
#define FILE_O_APPEND "a"
#define FILE_O_READWRITE "r+"
#endif

#endif // STUB_FS_LOADED
