#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace opennvr {

struct ProbeConfig {
    bool synthetic = true;
    bool substream = false;
    bool preview = false;
    bool test_recording = false; // Explicit manual/synthetic trigger; never presented as AI.
    bool detect_people = false;
    std::filesystem::path project_root;
    std::string host;
    std::string username;
    std::string password;
    std::chrono::seconds duration{12};
    std::filesystem::path output{".local/recordings"};
    std::function<bool()> cancelled;
    std::function<void(const std::string&)> progress;
};

struct ProbeReport {
    bool success = false;
    bool cancelled = false;
    std::size_t packets = 0;
    std::size_t keyframes = 0;
    std::size_t bytes = 0;
    std::size_t ring_bytes = 0;
    std::size_t recorded_packets = 0;
    std::size_t analyzed_frames = 0;
    std::size_t person_frames = 0;
    std::size_t confirmed_frames = 0;
    std::size_t events = 0;
    double average_inference_ms = 0;
    bool stopped_during_event = false;
    int width = 0;
    int height = 0;
    double elapsed_seconds = 0;
    double maximum_gop_seconds = 0;
    std::string caps;
    std::string error;
    std::filesystem::path recording;
};

void initialize_media();
std::vector<std::string> media_capabilities();
ProbeReport run_probe(const ProbeConfig& config);
bool validate_recording(const std::filesystem::path& file, bool hardware_decode);

} // namespace opennvr
