#include "TestUtil.h"
#include "mesh/MeshOffGridDeliveryCore.h"
#include <unity.h>

using namespace meshoffgrid;

void setUp(void) {}
void tearDown(void) {}

static MessageId makeId(uint8_t seed)
{
    MessageId id{};
    for (size_t i = 0; i < id.size(); ++i)
        id[i] = static_cast<uint8_t>(seed + i);
    return id;
}

void test_happy_path_reaches_delivered()
{
    TEST_ASSERT_TRUE(DeliveryStateMachine::canTransition(DeliveryState::Created, DeliveryState::Queued));
    TEST_ASSERT_TRUE(DeliveryStateMachine::canTransition(DeliveryState::Queued, DeliveryState::Routing));
    TEST_ASSERT_TRUE(DeliveryStateMachine::canTransition(DeliveryState::Routing, DeliveryState::Sending));
    TEST_ASSERT_TRUE(DeliveryStateMachine::canTransition(DeliveryState::Sending, DeliveryState::TransportAck));
    TEST_ASSERT_TRUE(DeliveryStateMachine::canTransition(DeliveryState::TransportAck, DeliveryState::RemoteAck));
    TEST_ASSERT_TRUE(DeliveryStateMachine::canTransition(DeliveryState::RemoteAck, DeliveryState::Delivered));
}

void test_retry_and_fallback_return_to_routing()
{
    TEST_ASSERT_TRUE(DeliveryStateMachine::canTransition(DeliveryState::Sending, DeliveryState::RetryWait));
    TEST_ASSERT_TRUE(DeliveryStateMachine::canTransition(DeliveryState::RetryWait, DeliveryState::Routing));
    TEST_ASSERT_TRUE(DeliveryStateMachine::canTransition(DeliveryState::Sending, DeliveryState::Fallback));
    TEST_ASSERT_TRUE(DeliveryStateMachine::canTransition(DeliveryState::Fallback, DeliveryState::Routing));
}

void test_terminal_states_do_not_restart()
{
    TEST_ASSERT_TRUE(DeliveryStateMachine::isTerminal(DeliveryState::Delivered));
    TEST_ASSERT_TRUE(DeliveryStateMachine::isTerminal(DeliveryState::Expired));
    TEST_ASSERT_TRUE(DeliveryStateMachine::isTerminal(DeliveryState::Failed));
    TEST_ASSERT_FALSE(DeliveryStateMachine::canTransition(DeliveryState::Delivered, DeliveryState::Routing));
    TEST_ASSERT_FALSE(DeliveryStateMachine::canTransition(DeliveryState::Expired, DeliveryState::Queued));
    TEST_ASSERT_FALSE(DeliveryStateMachine::canTransition(DeliveryState::Failed, DeliveryState::Queued));
}

void test_expiry_is_optional_and_inclusive()
{
    MessageEnvelope message;
    message.expiresAt = 1000;
    TEST_ASSERT_FALSE(DeliveryStateMachine::isExpired(message, 999));
    TEST_ASSERT_TRUE(DeliveryStateMachine::isExpired(message, 1000));

    message.expiresAt = 0;
    TEST_ASSERT_FALSE(DeliveryStateMachine::isExpired(message, 0xffffffffu));
}

void test_dedup_cache_rejects_duplicate()
{
    MessageDedupCache<3> cache;
    const auto id = makeId(1);
    TEST_ASSERT_TRUE(cache.remember(id));
    TEST_ASSERT_FALSE(cache.remember(id));
    TEST_ASSERT_TRUE(cache.contains(id));
    TEST_ASSERT_EQUAL_UINT32(1, cache.size());
}

void test_dedup_cache_is_bounded_and_rotates()
{
    MessageDedupCache<2> cache;
    const auto a = makeId(1);
    const auto b = makeId(20);
    const auto c = makeId(40);

    TEST_ASSERT_TRUE(cache.remember(a));
    TEST_ASSERT_TRUE(cache.remember(b));
    TEST_ASSERT_TRUE(cache.remember(c));
    TEST_ASSERT_EQUAL_UINT32(2, cache.size());
    TEST_ASSERT_FALSE(cache.contains(a));
    TEST_ASSERT_TRUE(cache.contains(b));
    TEST_ASSERT_TRUE(cache.contains(c));
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_happy_path_reaches_delivered);
    RUN_TEST(test_retry_and_fallback_return_to_routing);
    RUN_TEST(test_terminal_states_do_not_restart);
    RUN_TEST(test_expiry_is_optional_and_inclusive);
    RUN_TEST(test_dedup_cache_rejects_duplicate);
    RUN_TEST(test_dedup_cache_is_bounded_and_rotates);
    exit(UNITY_END());
}

void loop() {}
