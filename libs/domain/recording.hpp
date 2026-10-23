#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace opennvr {

enum class RecordingStage { event_started, segment_started, segment_finished, event_finished };

struct RecordingUpdate {
    RecordingStage stage{};
    std::string event_id;
    std::string camera_id;
    std::filesystem::path file;
    int segment = 0;
    std::int64_t start_utc_ms = 0;
    std::int64_t end_utc_ms = 0;
    std::string status;
    std::string reason;
};

} // namespace opennvr
