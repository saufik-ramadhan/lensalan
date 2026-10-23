#pragma once

#include "domain/event.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace opennvr {

struct EncodedPacket {
    MonoTime received{};
    std::uint64_t pts = 0;
    std::uint64_t dts = 0;
    std::uint64_t duration = 0;
    std::uint64_t generation = 0;
    bool keyframe = false;
    std::shared_ptr<const std::vector<std::byte>> data;
    std::string caps;
};

// Single-owner class: the media worker serializes push/snapshot operations.
class EncodedRing {
public:
    EncodedRing(MonoTime history, std::size_t max_bytes);
    void push(EncodedPacket packet);
    std::vector<EncodedPacket> snapshot_from(MonoTime target) const;
    void clear();
    std::size_t bytes() const noexcept { return bytes_; }
    std::size_t size() const noexcept { return packets_.size(); }

private:
    void pop_front();
    MonoTime history_;
    std::size_t max_bytes_;
    std::size_t bytes_ = 0;
    std::deque<EncodedPacket> packets_;
};

} // namespace opennvr
