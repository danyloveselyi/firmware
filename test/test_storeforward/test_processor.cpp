#include "modules/storeforward/core/StoreForwardProcessor.h"
#include "unity.h"

void test_should_store_valid_message(void)
{
    // Arrange
    StoreForwardProcessor processor;
    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
    packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    packet.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;

    // Act
    bool shouldStore = processor.shouldStore(packet);

    // Assert
    TEST_ASSERT_TRUE(shouldStore);
}

void test_should_not_store_duplicate(void)
{
    // Arrange
    StoreForwardProcessor processor;
    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
    packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    packet.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    packet.id = 12345;

    // Act - first time should store
    processor.record(packet);
    bool shouldStoreAgain = processor.shouldStore(packet);

    // Assert
    TEST_ASSERT_FALSE(shouldStoreAgain);
}

void test_getMessagesForNode_should_filter_correctly(void)
{
    // Arrange
    StoreForwardProcessor processor;
    const NodeNum TEST_NODE = 0x12345678;
    const NodeNum OTHER_NODE = 0x87654321;

    // Add a message sent to our test node
    meshtastic_MeshPacket packetForTestNode = meshtastic_MeshPacket_init_zero;
    packetForTestNode.to = TEST_NODE;
    packetForTestNode.from = OTHER_NODE;
    packetForTestNode.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    packetForTestNode.rx_time = 100;
    processor.record(packetForTestNode);

    // Add a broadcast message
    meshtastic_MeshPacket broadcast = meshtastic_MeshPacket_init_zero;
    broadcast.to = NODENUM_BROADCAST;
    broadcast.from = OTHER_NODE;
    broadcast.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    broadcast.rx_time = 200;
    processor.record(broadcast);

    // Add a message from our test node (should be filtered out)
    meshtastic_MeshPacket packetFromTestNode = meshtastic_MeshPacket_init_zero;
    packetFromTestNode.from = TEST_NODE;
    packetFromTestNode.to = OTHER_NODE;
    packetFromTestNode.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    packetFromTestNode.rx_time = 300;
    processor.record(packetFromTestNode);

    // Act
    auto messages = processor.getMessagesForNode(TEST_NODE, 0);

    // Assert
    TEST_ASSERT_EQUAL(2, messages.size());
    // Should contain the direct message and the broadcast, but not the message from the test node
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_should_store_valid_message);
    RUN_TEST(test_should_not_store_duplicate);
    RUN_TEST(test_getMessagesForNode_should_filter_correctly);
    UNITY_END();

    return 0;
}
