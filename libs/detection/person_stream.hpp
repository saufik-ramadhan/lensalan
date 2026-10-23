#pragma once

#include "detection/person_detector.hpp"
#include <optional>

namespace opennvr {

class PersonStream {
public:
    PersonStream(const std::filesystem::path& root, const std::string& host,
                 const std::string& username, const std::string& password);
    ~PersonStream();
    std::optional<DetectionResult> poll();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace opennvr
