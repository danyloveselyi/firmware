#pragma once

#include "../interfaces/ILogger.h" // Fix path to use relative path from current file
#include "../interfaces/IStorageBackend.h"

/**
 * Filesystem-based implementation of IStorageBackend
 * Uses FSCom to read/write data to the filesystem
 */
class FileSystemStorageBackend : public IStorageBackend
{
  public:
    /**
     * Constructor
     * @param logger The logger to use
     */
    explicit FileSystemStorageBackend(ILogger &logger);

    bool saveMessages(const std::vector<meshtastic_MeshPacket> &messages) override;
    std::vector<meshtastic_MeshPacket> loadMessages() override;
    bool saveRequestHistory(const std::unordered_map<NodeNum, uint32_t> &lastRequests) override;
    std::unordered_map<NodeNum, uint32_t> loadRequestHistory() override;
    bool isAvailable() const override;

  private:
    ILogger &logger;

    // Constants
    static constexpr const char *DIR_PATH = "/history";
    static constexpr const char *MESSAGES_FILE = "/history/sf";
    static constexpr const char *REQUESTS_FILE = "/history/sf_users";

    // Helper methods
    bool createHistoryDirectory();
    void logStoredMessageDetails(const meshtastic_MeshPacket &msg, int index);
};
