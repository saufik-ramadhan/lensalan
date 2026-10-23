#pragma once
#include "detection/person_detector.hpp"
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <atomic>

namespace opennvr {
struct AnalysisChannel {
    std::vector<std::uint8_t> pending;
    int width = 0, height = 0, stride = 0;
    std::optional<DetectionResult> result;
    std::string error;
    std::mutex mutex;
};
// One CUDA session, round-robin ready channels, at most one pending frame/channel.
class InferenceScheduler {
public:
    using Infer = std::function<DetectionResult(const std::uint8_t*, int, int, int)>;
    explicit InferenceScheduler(std::filesystem::path root, Infer test_infer = {});
    ~InferenceScheduler();
    std::shared_ptr<AnalysisChannel> channel();
    void submit(const std::shared_ptr<AnalysisChannel>& channel, const std::uint8_t* data, int width, int height, int stride);
    std::optional<DetectionResult> take(const std::shared_ptr<AnalysisChannel>& channel);
    int sessions() const { return sessions_.load(); }
    std::uint64_t inferred() const { return inferred_.load(); }
    std::uint64_t replaced() const { return replaced_.load(); }
private:
    void run();
    std::filesystem::path root_;
    Infer infer_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::vector<std::weak_ptr<AnalysisChannel>> channels_;
    bool stopping_ = false;
    std::atomic_int sessions_{0};
    std::atomic_uint64_t inferred_{0}, replaced_{0};
    std::jthread worker_;
};
}
