#pragma once

#include "detection/person_detector.hpp"
#include "detection/scheduler.hpp"
#include "media/encoded_ring.hpp"
#include <optional>

namespace opennvr {

class PersonStream {
public:
    PersonStream(const std::filesystem::path& root, const std::string& host,
                 const std::string& username, const std::string& password,
                 std::shared_ptr<InferenceScheduler> scheduler = {},
                 std::function<void(const EncodedPacket&)> live = {});
    ~PersonStream();
    std::optional<DetectionResult> poll();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace opennvr
