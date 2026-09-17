#pragma once

#include "LinkTransport.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace meshtastic::multilink {

struct SelectionPolicy {
    uint16_t minDeliveryPermille = 250;
    uint16_t hysteresisScore = 120;
    uint32_t stickyWindowMs = 4000;
    uint32_t metricsMaxAgeMs = 15000;
    bool duplicateControlTraffic = true;
};

struct LinkCandidate {
    LinkTransport *link = nullptr;
    int32_t score = -1000000;
};

struct SendPlan {
    std::array<LinkTransport *, 2> links{};
    uint8_t count = 0;
};

class AdaptiveLinkManager
{
  public:
    static constexpr size_t MaxLinks = 5;
    static constexpr size_t DedupeSlots = 64;

    explicit AdaptiveLinkManager(SelectionPolicy policy = {});

    bool addLink(LinkTransport &link);
    void poll(uint32_t nowMs);
    SendPlan select(const FrameView &frame, uint32_t nowMs);
    SendResult send(const FrameView &frame, uint32_t nowMs);

    bool seenRecently(uint32_t from, uint32_t messageId, uint32_t nowMs, uint32_t ttlMs = 120000);
    void remember(uint32_t from, uint32_t messageId, uint32_t nowMs);

    static int32_t score(const LinkTransport &link, const FrameView &frame, uint32_t nowMs,
                         const SelectionPolicy &policy);

  private:
    struct StickySelection {
        LinkType type = LinkType::LoRa;
        int32_t score = -1000000;
        uint32_t selectedAtMs = 0;
        bool valid = false;
    };

    struct DedupeEntry {
        uint32_t from = 0;
        uint32_t messageId = 0;
        uint32_t seenAtMs = 0;
        bool valid = false;
    };

    SelectionPolicy policy_;
    std::array<LinkTransport *, MaxLinks> links_{};
    size_t linkCount_ = 0;
    std::array<StickySelection, 4> sticky_{};
    std::array<DedupeEntry, DedupeSlots> dedupe_{};
    size_t dedupeWrite_ = 0;

    LinkTransport *find(LinkType type) const;
    static size_t trafficIndex(TrafficClass trafficClass);
};

} // namespace meshtastic::multilink
