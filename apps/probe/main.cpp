#include "media/probe.hpp"

#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    opennvr::ProbeConfig config;
    config.progress = [](const auto& message) { std::cout << message << '\n'; };
    bool self_test = false;
    bool hardware = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--capabilities") {
            for (const auto& item : opennvr::media_capabilities()) std::cout << item << '\n';
            return 0;
        }
        if (arg == "--self-test") { self_test = true; config.test_recording = true; }
        else if (arg == "--hardware") hardware = true;
        else if (arg == "--preview") config.preview = true;
        else if (arg == "--output" && i + 1 < argc) config.output = argv[++i];
        else {
            std::cerr << "Usage: opennvr_probe --capabilities | --self-test [--hardware] [--preview] [--output DIR]\n";
            return 2;
        }
    }
    if (!self_test) {
        std::cout << "Use --self-test to generate a synthetic H.264 event clip, or --capabilities.\n"
                     "Camera credentials are entered through the OpenNVR desktop console.\n";
        return 0;
    }
    const auto report = opennvr::run_probe(config);
    if (!report.success) { std::cerr << report.error << '\n'; return 1; }
    const bool decoded = opennvr::validate_recording(report.recording, hardware);
    std::cout << "packets=" << report.packets << " keyframes=" << report.keyframes
              << " recorded=" << report.recorded_packets << " ring_bytes=" << report.ring_bytes
              << " decoded=" << (decoded ? "yes" : "no") << '\n';
    std::cout << "recording=" << report.recording.generic_string() << '\n';
    return decoded && report.recorded_packets > 0 ? 0 : 1;
}
