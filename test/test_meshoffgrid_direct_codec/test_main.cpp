#include "TestUtil.h"
#include "mesh/MeshOffGridDirectCodec.h"
#include <unity.h>

#include <array>

using namespace meshoffgrid;

#if defined(ARCH_PORTDUINO)
#define V14_TEST_ENTRY extern "C"
#else
#define V14_TEST_ENTRY
#endif

void setUp(void) {}
void tearDown(void) {}

void test_hello_round_trips()
{
    DirectFrame frame;
    size_t length = 0;
    TEST_ASSERT_TRUE(buildDirectHello(0x12345678, frame, length));
    TEST_ASSERT_EQUAL_UINT32(sizeof(DirectFrameHeader), length);

    DirectFrame parsed;
    TEST_ASSERT_TRUE(parseDirectFrame(reinterpret_cast<const uint8_t *>(&frame), length, parsed));
    TEST_ASSERT_EQUAL_UINT32(0x12345678, parsed.header.senderNode);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DirectFrameType::Hello), parsed.header.type);
}

void test_max_message_fragments_and_reassembles()
{
    std::array<uint8_t, kDirectMaxMessageBytes> input{};
    for (size_t i = 0; i < input.size(); ++i)
        input[i] = static_cast<uint8_t>(i & 0xff);

    TEST_ASSERT_EQUAL_UINT32(3, directFragmentCount(input.size()));

    DirectReassemblyBuffer buffer;
    TEST_ASSERT_TRUE(buffer.reset(0x10203040, 0x55667788, input.size(), 3));

    for (uint8_t i = 0; i < 3; ++i) {
        DirectFrame frame;
        size_t length = 0;
        TEST_ASSERT_TRUE(buildDirectDataFrame(0x10203040, 0x55667788, input.data(), input.size(), i, frame, length));

        DirectFrame parsed;
        TEST_ASSERT_TRUE(parseDirectFrame(reinterpret_cast<const uint8_t *>(&frame), length, parsed));
        TEST_ASSERT_TRUE(buffer.accept(parsed));
    }

    TEST_ASSERT_TRUE(buffer.complete());
    TEST_ASSERT_EQUAL_UINT32(input.size(), buffer.length());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(input.data(), buffer.data(), input.size());
}

void test_out_of_order_fragments_reassemble()
{
    std::array<uint8_t, 300> input{};
    for (size_t i = 0; i < input.size(); ++i)
        input[i] = static_cast<uint8_t>(255 - (i & 0xff));

    DirectReassemblyBuffer buffer;
    TEST_ASSERT_TRUE(buffer.reset(42, 99, input.size(), 2));

    for (uint8_t index : {1, 0}) {
        DirectFrame frame;
        size_t length = 0;
        TEST_ASSERT_TRUE(buildDirectDataFrame(42, 99, input.data(), input.size(), index, frame, length));
        TEST_ASSERT_TRUE(buffer.accept(frame));
    }

    TEST_ASSERT_TRUE(buffer.complete());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(input.data(), buffer.data(), input.size());
}

void test_duplicate_fragment_is_idempotent()
{
    std::array<uint8_t, 230> input{};
    DirectReassemblyBuffer buffer;
    TEST_ASSERT_TRUE(buffer.reset(42, 100, input.size(), 2));

    DirectFrame first;
    size_t length = 0;
    TEST_ASSERT_TRUE(buildDirectDataFrame(42, 100, input.data(), input.size(), 0, first, length));
    TEST_ASSERT_TRUE(buffer.accept(first));
    TEST_ASSERT_TRUE(buffer.accept(first));
    TEST_ASSERT_FALSE(buffer.complete());
}

void test_malformed_offset_is_rejected()
{
    std::array<uint8_t, 300> input{};
    DirectFrame frame;
    size_t length = 0;
    TEST_ASSERT_TRUE(buildDirectDataFrame(42, 100, input.data(), input.size(), 1, frame, length));
    frame.header.fragmentOffset = 1;

    DirectFrame parsed;
    TEST_ASSERT_FALSE(parseDirectFrame(reinterpret_cast<const uint8_t *>(&frame), length, parsed));
}

void test_oversize_message_is_rejected()
{
    std::array<uint8_t, kDirectMaxMessageBytes + 1> input{};
    DirectFrame frame;
    size_t length = 0;
    TEST_ASSERT_EQUAL_UINT32(0, directFragmentCount(input.size()));
    TEST_ASSERT_FALSE(buildDirectDataFrame(42, 100, input.data(), input.size(), 0, frame, length));
}

V14_TEST_ENTRY void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_hello_round_trips);
    RUN_TEST(test_max_message_fragments_and_reassembles);
    RUN_TEST(test_out_of_order_fragments_reassemble);
    RUN_TEST(test_duplicate_fragment_is_idempotent);
    RUN_TEST(test_malformed_offset_is_rejected);
    RUN_TEST(test_oversize_message_is_rejected);
    exit(UNITY_END());
}

V14_TEST_ENTRY void loop() {}
