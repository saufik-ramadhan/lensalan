#include "media/encoded_ring.hpp"

#include <algorithm>
#include <stdexcept>

namespace opennvr {

EncodedRing::EncodedRing(MonoTime history, std::size_t max_bytes)
    : history_(history), max_bytes_(max_bytes) {
    if (history <= MonoTime::zero() || max_bytes == 0) {
        throw std::invalid_argument("Ring history and memory limit must be positive");
    }
}

void EncodedRing::pop_front() {
    bytes_ -= packets_.front().data->size();
    packets_.pop_front();
}

void EncodedRing::clear() {
    packets_.clear();
    bytes_ = 0;
}

void EncodedRing::push(EncodedPacket packet) {
    if (!packet.data || packet.data->empty()) return;
    if (!packets_.empty() && (packet.generation != packets_.back().generation ||
        packet.caps != packets_.back().caps || packet.received < packets_.back().received)) {
        clear();
    }
    if (packet.data->size() > max_bytes_) {
        clear();
        return;
    }
    if (packets_.empty() && !packet.keyframe) return;
    bytes_ += packet.data->size();
    packets_.push_back(std::move(packet));

    // Retain the GOP that contains the cutoff. Arbitrary frame dropping breaks H.264 playback.
    const auto cutoff = packets_.back().received - history_;
    std::size_t anchor = 0;
    for (std::size_t i = 1; i < packets_.size(); ++i) {
        if (packets_[i].keyframe && packets_[i].received <= cutoff) anchor = i;
    }
    while (anchor > 0) { pop_front(); --anchor; }

    while (bytes_ > max_bytes_) pop_front();
    while (!packets_.empty() && !packets_.front().keyframe) pop_front();
}

std::vector<EncodedPacket> EncodedRing::snapshot_from(MonoTime target) const {
    if (packets_.empty()) return {};
    std::size_t anchor = 0;
    for (std::size_t i = 0; i < packets_.size(); ++i) {
        if (packets_[i].keyframe && packets_[i].received <= target) anchor = i;
    }
    return {packets_.begin() + static_cast<std::ptrdiff_t>(anchor), packets_.end()};
}

} // namespace opennvr
