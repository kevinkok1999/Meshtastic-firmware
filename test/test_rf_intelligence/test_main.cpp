#include "mesh/RFIntelligence.h"
#include <unity.h>

using meshoffgrid::RFIntelligenceSnapshot;
using meshoffgrid::RxGainDecision;

static void test_no_measurements_keeps_current_gain()
{
    RFIntelligenceSnapshot s;
    s.hasNoiseFloor = false;
    TEST_ASSERT_EQUAL(static_cast<int>(RxGainDecision::KEEP),
                      static_cast<int>(meshoffgrid::chooseRxGain(s, false)));
}

static void test_clean_weak_signal_enables_boost()
{
    RFIntelligenceSnapshot s;
    s.hasNoiseFloor = true;
    s.noiseFloorDbm = -116;
    s.hasPacketMetrics = true;
    s.packetRssiDbm = -111.0f;
    s.packetSnrDb = 0.5f;
    s.hasCadMetrics = true;
    s.cadBusyPermille = 180;
    s.hasErrorMetrics = true;
    s.packetErrorPermille = 80;

    TEST_ASSERT_EQUAL(static_cast<int>(RxGainDecision::BOOSTED),
                      static_cast<int>(meshoffgrid::chooseRxGain(s, false)));
}

static void test_high_noise_forces_power_save()
{
    RFIntelligenceSnapshot s;
    s.hasNoiseFloor = true;
    s.noiseFloorDbm = -99;
    s.hasPacketMetrics = true;
    s.packetRssiDbm = -108.0f;
    s.packetSnrDb = -1.0f;

    TEST_ASSERT_EQUAL(static_cast<int>(RxGainDecision::POWER_SAVE),
                      static_cast<int>(meshoffgrid::chooseRxGain(s, true)));
}

static void test_busy_noisy_channel_forces_power_save()
{
    RFIntelligenceSnapshot s;
    s.hasNoiseFloor = true;
    s.noiseFloorDbm = -106;
    s.hasCadMetrics = true;
    s.cadBusyPermille = 850;

    TEST_ASSERT_EQUAL(static_cast<int>(RxGainDecision::POWER_SAVE),
                      static_cast<int>(meshoffgrid::chooseRxGain(s, true)));
}

static void test_hysteresis_keeps_boost_in_quiet_band()
{
    RFIntelligenceSnapshot s;
    s.hasNoiseFloor = true;
    s.noiseFloorDbm = -109;
    s.hasPacketMetrics = true;
    s.packetRssiDbm = -104.0f;
    s.packetSnrDb = 4.0f;
    s.hasCadMetrics = true;
    s.cadBusyPermille = 400;
    s.hasErrorMetrics = true;
    s.packetErrorPermille = 100;

    TEST_ASSERT_EQUAL(static_cast<int>(RxGainDecision::KEEP),
                      static_cast<int>(meshoffgrid::chooseRxGain(s, true)));
}

static void test_decode_error_storm_forces_power_save()
{
    RFIntelligenceSnapshot s;
    s.hasNoiseFloor = true;
    s.noiseFloorDbm = -114;
    s.hasErrorMetrics = true;
    s.packetErrorPermille = 600;

    TEST_ASSERT_EQUAL(static_cast<int>(RxGainDecision::POWER_SAVE),
                      static_cast<int>(meshoffgrid::chooseRxGain(s, true)));
}

static void test_quality_score_penalizes_interference()
{
    RFIntelligenceSnapshot clean;
    clean.hasNoiseFloor = true;
    clean.noiseFloorDbm = -118;
    clean.hasCadMetrics = true;
    clean.cadBusyPermille = 100;
    clean.hasErrorMetrics = true;
    clean.packetErrorPermille = 20;

    RFIntelligenceSnapshot noisy = clean;
    noisy.noiseFloorDbm = -100;
    noisy.cadBusyPermille = 900;
    noisy.packetErrorPermille = 500;

    TEST_ASSERT_GREATER_THAN(meshoffgrid::rfQualityScore(noisy), meshoffgrid::rfQualityScore(clean));
}

void setUp(void) {}
void tearDown(void) {}

void setup()
{
    UNITY_BEGIN();
    RUN_TEST(test_no_measurements_keeps_current_gain);
    RUN_TEST(test_clean_weak_signal_enables_boost);
    RUN_TEST(test_high_noise_forces_power_save);
    RUN_TEST(test_busy_noisy_channel_forces_power_save);
    RUN_TEST(test_hysteresis_keeps_boost_in_quiet_band);
    RUN_TEST(test_decode_error_storm_forces_power_save);
    RUN_TEST(test_quality_score_penalizes_interference);
    exit(UNITY_END());
}

void loop() {}
