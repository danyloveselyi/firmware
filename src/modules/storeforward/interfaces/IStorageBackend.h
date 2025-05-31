#pragma once

#include <cstdint>
#include <string>

/**
 * Interface for storage backends
 */
class IStorageBackend
{
  public:
    virtual ~IStorageBackend() = default;

    // Basic file operations
    virtual bool fileExists(const std::string &filename) const = 0;
    virtual bool readFile(const std::string &filename, uint8_t *buffer, size_t maxSize, size_t *actualSize) = 0;
    virtual bool writeFile(const std::string &filename, const uint8_t *data, size_t size) = 0;
    virtual bool deleteFile(const std::string &filename) = 0;

    // Directory operations
    virtual bool createDirectory(const std::string &path) = 0;
    virtual bool directoryExists(const std::string &path) const = 0;
};
