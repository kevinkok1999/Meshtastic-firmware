// Pins the V14 route-selection policy before any hardware transport is integrated. These tests guard
// against regressions where unavailable links win, route flapping bypasses hysteresis, or a delivery
// intent accidentally optimizes for the wrong link property.
#include "TestUtil.h"
#include "mesh/MeshOffGridRouteBrain.h"
#include <unity.h>

using namespace meshoffgrid;

void setUp(void) {}
void tearDown(void) {}

static RouteCandidate candidate(TransportKind kind, uint8_t reliability, uint8_t latency, uint8_t throughput, uint8_t energy,
                                uint8_t airtime, uint8_t confidence, bool bulk = false, bool available = true)
{
    RouteCandidate result;
    result.kind = kind;
    result.metrics.available = available;
    result.metrics.bulkCapable = bulk;
    result.metrics.reliability = reliability;
    result.metrics.latencyQuality = latency;
    result.metrics.throughput = throughput;
    result.metrics.energyEfficiency = energy;
    result.metrics.airtimeEfficiency = airtime;
    result.metrics.confidence = confidence;
    return result;
}

void test_unavailable_link_never_wins()
{
    const RouteCandidate links[] = {
        candidate(TransportKind::DirectLink, 100, 100, 100, 100, 100, 100, true, false),
        candidate(TransportKind::LongLink, 70, 50, 20, 70, 70, 80),
    };

    const auto result = RouteBrain::select(links, 2, DeliveryIntent::Reliable, 64);
    TEST_ASSERT_TRUE(result.hasPrimary);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransportKind::LongLink), static_cast<uint8_t>(result.primary));
}

void test_reliable_intent_prefers_reliability_and_confidence()
{
    const RouteCandidate links[] = {
        candidate(TransportKind::LongLink, 98, 50, 30, 50, 50, 98),
        candidate(TransportKind::DirectLink, 72, 95, 95, 80, 80, 70),
    };

    const auto result = RouteBrain::select(links, 2, DeliveryIntent::Reliable, 80);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransportKind::LongLink), static_cast<uint8_t>(result.primary));
}

void test_fast_intent_prefers_latency_and_throughput()
{
    const RouteCandidate links[] = {
        candidate(TransportKind::LongLink, 98, 35, 20, 80, 80, 95),
        candidate(TransportKind::DirectLink, 88, 98, 96, 70, 70, 90),
    };

    const auto result = RouteBrain::select(links, 2, DeliveryIntent::Fast, 80);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransportKind::DirectLink), static_cast<uint8_t>(result.primary));
}

void test_low_power_intent_prefers_energy_and_airtime_efficiency()
{
    const RouteCandidate links[] = {
        candidate(TransportKind::FastLink, 90, 98, 100, 35, 30, 90, true),
        candidate(TransportKind::LongLink, 86, 45, 25, 98, 98, 90),
    };

    const auto result = RouteBrain::select(links, 2, DeliveryIntent::LowPower, 64);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransportKind::LongLink), static_cast<uint8_t>(result.primary));
}

void test_bulk_intent_prefers_bulk_capable_link()
{
    const RouteCandidate links[] = {
        candidate(TransportKind::LongLink, 100, 80, 80, 90, 90, 100, false),
        candidate(TransportKind::FastLink, 88, 90, 95, 65, 65, 90, true),
    };

    const auto result = RouteBrain::select(links, 2, DeliveryIntent::Bulk, 4096);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransportKind::FastLink), static_cast<uint8_t>(result.primary));
}

void test_hysteresis_keeps_current_route_for_small_improvement()
{
    const RouteCandidate links[] = {
        candidate(TransportKind::LongLink, 90, 80, 70, 80, 80, 90),
        candidate(TransportKind::DirectLink, 92, 82, 72, 80, 80, 92),
    };

    const auto result = RouteBrain::select(links, 2, DeliveryIntent::Reliable, 64, true, TransportKind::LongLink, 80);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransportKind::LongLink), static_cast<uint8_t>(result.primary));
    TEST_ASSERT_TRUE(result.hasBackup);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransportKind::DirectLink), static_cast<uint8_t>(result.backup));
}

void test_hysteresis_switches_for_material_improvement()
{
    const RouteCandidate links[] = {
        candidate(TransportKind::LongLink, 60, 50, 30, 60, 60, 60),
        candidate(TransportKind::DirectLink, 98, 95, 95, 85, 85, 98),
    };

    const auto result = RouteBrain::select(links, 2, DeliveryIntent::Reliable, 64, true, TransportKind::LongLink, 80);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransportKind::DirectLink), static_cast<uint8_t>(result.primary));
}

void test_backup_is_best_remaining_available_route()
{
    const RouteCandidate links[] = {
        candidate(TransportKind::LongLink, 92, 60, 40, 80, 90, 92),
        candidate(TransportKind::DirectLink, 96, 96, 94, 80, 80, 95),
        candidate(TransportKind::NetLink, 80, 90, 90, 50, 50, 80, true),
    };

    const auto result = RouteBrain::select(links, 3, DeliveryIntent::Reliable, 64);
    TEST_ASSERT_TRUE(result.hasPrimary);
    TEST_ASSERT_TRUE(result.hasBackup);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransportKind::DirectLink), static_cast<uint8_t>(result.primary));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransportKind::LongLink), static_cast<uint8_t>(result.backup));
}

void test_equal_scores_have_stable_transport_order()
{
    const RouteCandidate links[] = {
        candidate(TransportKind::DirectLink, 80, 80, 80, 80, 80, 80),
        candidate(TransportKind::LongLink, 80, 80, 80, 80, 80, 80),
    };

    const auto result = RouteBrain::select(links, 2, DeliveryIntent::Reliable, 64);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransportKind::LongLink), static_cast<uint8_t>(result.primary));
}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_unavailable_link_never_wins);
    RUN_TEST(test_reliable_intent_prefers_reliability_and_confidence);
    RUN_TEST(test_fast_intent_prefers_latency_and_throughput);
    RUN_TEST(test_low_power_intent_prefers_energy_and_airtime_efficiency);
    RUN_TEST(test_bulk_intent_prefers_bulk_capable_link);
    RUN_TEST(test_hysteresis_keeps_current_route_for_small_improvement);
    RUN_TEST(test_hysteresis_switches_for_material_improvement);
    RUN_TEST(test_backup_is_best_remaining_available_route);
    RUN_TEST(test_equal_scores_have_stable_transport_order);
    exit(UNITY_END());
}

void loop() {}
