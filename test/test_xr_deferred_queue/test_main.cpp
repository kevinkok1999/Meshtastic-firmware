#include "TestUtil.h"
#include "xr/XRDeferredPacketQueue.h"

#include <cstdlib>
#include <unity.h>

using namespace meshoffgrid::xr;

void setUp(void) {}
void tearDown(void) {}

static meshtastic_MeshPacket makeEncrypted(uint32_t id, uint32_t to)
{
    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
    packet.id = id;
    packet.to = to;
    packet.from = 0x12345678;
    packet.which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    packet.encrypted.size = 3;
    packet.encrypted.bytes[0] = 1;
    packet.encrypted.bytes[1] = 2;
    packet.encrypted.bytes[2] = 3;
    return packet;
}

void test_enqueue_deduplicates_same_packet()
{
    XRDeferredPacketQueue q;
    auto packet = makeEncrypted(10, 20);
    TEST_ASSERT_TRUE(q.enqueue(packet, 1000));
    TEST_ASSERT_TRUE(q.enqueue(packet, 2000));
    TEST_ASSERT_EQUAL_UINT32(1, q.size());
}

void test_plaintext_packet_is_rejected()
{
    XRDeferredPacketQueue q;
    auto packet = makeEncrypted(10, 20);
    packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    TEST_ASSERT_FALSE(q.enqueue(packet, 1000));
    TEST_ASSERT_TRUE(q.empty());
}

void test_success_removes_packet()
{
    XRDeferredPacketQueue q;
    auto packet = makeEncrypted(10, 20);
    TEST_ASSERT_TRUE(q.enqueue(packet, 1000));
    q.markSuccess(10, 20);
    TEST_ASSERT_TRUE(q.empty());
}

void test_failure_backs_off_without_dropping()
{
    XRDeferredPacketQueue q;
    auto packet = makeEncrypted(10, 20);
    TEST_ASSERT_TRUE(q.enqueue(packet, 1000));
    q.markFailure(10, 20, 2000);

    auto *entry = q.entry(0);
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_TRUE(entry->used);
    TEST_ASSERT_EQUAL_UINT8(1, entry->attempts);
    TEST_ASSERT_TRUE(entry->nextAttemptMs > 2000);
}

void test_expiry_removes_old_packet()
{
    XRDeferredPacketQueue q;
    auto packet = makeEncrypted(10, 20);
    TEST_ASSERT_TRUE(q.enqueue(packet, 1000));
    q.expire(2001, 1000);
    TEST_ASSERT_TRUE(q.empty());
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_enqueue_deduplicates_same_packet);
    RUN_TEST(test_plaintext_packet_is_rejected);
    RUN_TEST(test_success_removes_packet);
    RUN_TEST(test_failure_backs_off_without_dropping);
    RUN_TEST(test_expiry_removes_old_packet);
    std::exit(UNITY_END());
}

void loop() {}
