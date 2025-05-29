#include "FileSystemStorageBackend.h"
#include "FSCommon.h"
#include "NodeDB.h"
#include <algorithm>

FileSystemStorageBackend::FileSystemStorageBackend(ILogger &logger) : logger(logger)
{
    // Ensure directory exists
    createHistoryDirectory();
}

bool FileSystemStorageBackend::createHistoryDirectory()
{
#ifdef FSCom
    if (!FSCom.exists(DIR_PATH)) {
        logger.info("S&F: Creating directory %s", DIR_PATH);
        return FSCom.mkdir(DIR_PATH);
    }
    return true;
#else
    logger.warn("S&F: Filesystem not implemented, can't create directory");
    return false;
#endif
}

bool FileSystemStorageBackend::saveMessages(const std::vector<meshtastic_MeshPacket> &messages)
{
#ifdef FSCom
    logger.info("S&F: Saving %u messages to %s", (unsigned)messages.size(), MESSAGES_FILE);

    File storeAndForward = FSCom.open(MESSAGES_FILE, FILE_O_WRITE);
    if (!storeAndForward) {
        logger.error("S&F: Could not open %s for writing", MESSAGES_FILE);
        return false;
    }

    uint32_t totalSize = sizeof(meshtastic_MeshPacket) * messages.size();
    uint32_t written = storeAndForward.write((uint8_t *)messages.data(), totalSize);
    storeAndForward.close();

    if (written == totalSize) {
        logger.info("S&F: Successfully stored %u messages (%u bytes) to flash", (unsigned)messages.size(), written);
        return true;
    } else {
        logger.error("S&F: Error writing messages to flash: %u of %u bytes written", written, totalSize);
        return false;
    }
#else
    logger.warn("S&F: Filesystem not implemented, can't save messages");
    return false;
#endif
}

std::vector<meshtastic_MeshPacket> FileSystemStorageBackend::loadMessages()
{
    std::vector<meshtastic_MeshPacket> messages;

#ifdef FSCom
    logger.info("S&F: Checking if message history file exists at %s", MESSAGES_FILE);

    if (!FSCom.exists(MESSAGES_FILE)) {
        logger.info("S&F: No history file found, starting with empty history");
        return messages;
    }

    File storeAndForward = FSCom.open(MESSAGES_FILE, FILE_O_READ);
    if (!storeAndForward) {
        logger.error("S&F: Could not open history file for reading");
        return messages;
    }

    size_t fileSize = storeAndForward.size();
    uint32_t numRecords = fileSize / sizeof(meshtastic_MeshPacket);

    logger.info("S&F: Found file with %u bytes (%u potential messages)", (unsigned)fileSize, numRecords);

    if (numRecords > 0) {
        messages.resize(numRecords);
        uint32_t bytesRead = storeAndForward.read((uint8_t *)messages.data(), fileSize);
        logger.info("S&F: Loaded %u messages from flash (%u bytes)", numRecords, bytesRead);
    }

    storeAndForward.close();
#else
    logger.warn("S&F: Filesystem not implemented, can't load messages");
#endif

    return messages;
}

bool FileSystemStorageBackend::saveRequestHistory(const std::unordered_map<NodeNum, uint32_t> &lastRequests)
{
#ifdef FSCom
    logger.info("S&F: Saving user request history for %u users", (unsigned)lastRequests.size());

    File userRequestsFile = FSCom.open(REQUESTS_FILE, FILE_O_WRITE);
    if (!userRequestsFile) {
        logger.error("S&F: Could not open user requests file for writing");
        return false;
    }

    // Write number of entries first
    size_t entriesCount = lastRequests.size();
    userRequestsFile.write((uint8_t *)&entriesCount, sizeof(entriesCount));

    // Write each user's last request position
    for (const auto &entry : lastRequests) {
        userRequestsFile.write((uint8_t *)&entry.first, sizeof(entry.first));   // NodeNum
        userRequestsFile.write((uint8_t *)&entry.second, sizeof(entry.second)); // lastRequest index

        // Get user name if available
        meshtastic_NodeInfoLite *userNode = nodeDB->getMeshNode(entry.first);
        const char *userName = (userNode && userNode->has_user && userNode->user.long_name[0])    ? userNode->user.long_name
                               : (userNode && userNode->has_user && userNode->user.short_name[0]) ? userNode->user.short_name
                                                                                                  : "Unknown";

        logger.info("S&F: Saved user %s (0x%08x) last request: %u", userName, entry.first, entry.second);
    }

    userRequestsFile.close();
    logger.info("S&F: User request history saved successfully");
    return true;
#else
    logger.warn("S&F: Filesystem not implemented, can't save request history");
    return false;
#endif
}

std::unordered_map<NodeNum, uint32_t> FileSystemStorageBackend::loadRequestHistory()
{
    std::unordered_map<NodeNum, uint32_t> lastRequests;

#ifdef FSCom
    logger.info("S&F: Checking for user request history file at %s", REQUESTS_FILE);

    if (!FSCom.exists(REQUESTS_FILE)) {
        logger.info("S&F: No user request history file found");
        return lastRequests;
    }

    File userRequestsFile = FSCom.open(REQUESTS_FILE, FILE_O_READ);
    if (!userRequestsFile) {
        logger.error("S&F: Could not open user requests file for reading");
        return lastRequests;
    }

    // Read number of entries
    size_t entriesCount = 0;
    userRequestsFile.read((uint8_t *)&entriesCount, sizeof(entriesCount));
    logger.info("S&F: Found request history for %u users", entriesCount);

    // Read each entry
    for (size_t i = 0; i < entriesCount; i++) {
        NodeNum nodeId;
        uint32_t lastIdx;

        userRequestsFile.read((uint8_t *)&nodeId, sizeof(nodeId));
        userRequestsFile.read((uint8_t *)&lastIdx, sizeof(lastIdx));

        // Store in map
        lastRequests[nodeId] = lastIdx;

        // Get user name if available
        meshtastic_NodeInfoLite *userNode = nodeDB->getMeshNode(nodeId);
        const char *userName = (userNode && userNode->has_user && userNode->user.long_name[0])    ? userNode->user.long_name
                               : (userNode && userNode->has_user && userNode->user.short_name[0]) ? userNode->user.short_name
                                                                                                  : "Unknown";

        logger.info("S&F: Loaded user %s (0x%08x) with lastRequest: %u", userName, nodeId, lastIdx);
    }

    userRequestsFile.close();
    logger.info("S&F: User request history loaded successfully");
#else
    logger.warn("S&F: Filesystem not implemented, can't load request history");
#endif

    return lastRequests;
}

bool FileSystemStorageBackend::isAvailable() const
{
#ifdef FSCom
    return true;
#else
    return false;
#endif
}

void FileSystemStorageBackend::logStoredMessageDetails(const meshtastic_MeshPacket &msg, int index)
{
    // Get sender name from node DB if available
    meshtastic_NodeInfoLite *senderNode = nodeDB->getMeshNode(msg.from);
    const char *senderName = (senderNode && senderNode->has_user && senderNode->user.long_name[0]) ? senderNode->user.long_name
                             : (senderNode && senderNode->has_user && senderNode->user.short_name[0])
                                 ? senderNode->user.short_name
                                 : "Unknown";

    // Get recipient name if direct message
    const char *recipientName = "BROADCAST";
    if (msg.to != NODENUM_BROADCAST) {
        meshtastic_NodeInfoLite *recipientNode = nodeDB->getMeshNode(msg.to);
        recipientName =
            (recipientNode && recipientNode->has_user && recipientNode->user.long_name[0])    ? recipientNode->user.long_name
            : (recipientNode && recipientNode->has_user && recipientNode->user.short_name[0]) ? recipientNode->user.short_name
                                                                                              : "Unknown";
    }

    // Log detailed message info
    logger.info("S&F: Message %d - from: %s (0x%x), to: %s (0x%x), time: %u", index, senderName, msg.from, recipientName, msg.to,
                msg.rx_time);
}
