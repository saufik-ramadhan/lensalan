#pragma once

#include "media/encoded_ring.hpp"

#include <filesystem>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>

namespace opennvr {

class EventWriter {
public:
    EventWriter(const std::filesystem::path& path, const EncodedPacket& first);
    ~EventWriter();
    EventWriter(const EventWriter&) = delete;
    EventWriter& operator=(const EventWriter&) = delete;
    void push(const EncodedPacket& packet);
    void finish();
    std::size_t packets() const noexcept { return packets_; }

private:
    void close() noexcept;
    GstElement* pipeline_ = nullptr;
    GstAppSrc* source_ = nullptr;
    std::uint64_t base_ = 0;
    std::size_t packets_ = 0;
    std::uint64_t generation_ = 0;
    std::string caps_;
};

} // namespace opennvr
