#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace opennvr {

struct PersonBox { float x, y, width, height, confidence; };
struct DetectionResult {
    std::vector<PersonBox> people;
    double inference_ms = 0;
};

class PersonDetector {
public:
    explicit PersonDetector(const std::filesystem::path& project_root, float confidence = 0.45F);
    ~PersonDetector();
    PersonDetector(const PersonDetector&) = delete;
    PersonDetector& operator=(const PersonDetector&) = delete;
    DetectionResult infer(const std::uint8_t* bgr, int width, int height, int stride);
    const std::string& provider() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace opennvr
