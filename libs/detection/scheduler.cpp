#include "detection/scheduler.hpp"
#include <algorithm>
#include <stdexcept>

namespace opennvr {
InferenceScheduler::InferenceScheduler(std::filesystem::path root, Infer test_infer)
    : root_(std::move(root)), infer_(std::move(test_infer)), worker_([this] { run(); }) {}
InferenceScheduler::~InferenceScheduler() {
    { std::lock_guard lock(mutex_); stopping_ = true; }
    wake_.notify_all();
    worker_.join();
}
std::shared_ptr<AnalysisChannel> InferenceScheduler::channel() {
    auto value = std::make_shared<AnalysisChannel>();
    std::lock_guard lock(mutex_);
    std::erase_if(channels_, [](const auto& entry) { return entry.expired(); });
    if (channels_.size() >= 12) throw std::runtime_error("Maximum 12 inference channels");
    channels_.push_back(value);
    return value;
}
void InferenceScheduler::submit(const std::shared_ptr<AnalysisChannel>& channel, const std::uint8_t* data, int width, int height, int stride) {
    if (width < 1 || height < 1 || width > 1920 || height > 1080 || stride < width * 3 || stride > 8192)
        throw std::runtime_error("Unsupported analysis frame dimensions");
    { std::lock_guard lock(channel->mutex);
      if (!channel->pending.empty()) ++replaced_;
      channel->pending.assign(data, data + static_cast<std::size_t>(height) * stride);
      channel->width = width; channel->height = height; channel->stride = stride; }
    wake_.notify_one();
}
std::optional<DetectionResult> InferenceScheduler::take(const std::shared_ptr<AnalysisChannel>& channel) {
    std::lock_guard lock(channel->mutex);
    if (!channel->error.empty()) throw std::runtime_error(channel->error);
    auto result = std::move(channel->result);
    channel->result.reset();
    return result;
}
void InferenceScheduler::run() {
    std::unique_ptr<PersonDetector> detector;
    std::size_t cursor = 0;
    while (true) {
        std::shared_ptr<AnalysisChannel> selected;
        std::vector<std::uint8_t> frame;
        int width = 0, height = 0, stride = 0;
        {
            std::unique_lock lock(mutex_);
            if (stopping_) return;
            for (std::size_t n = 0; n < channels_.size(); ++n) {
                const auto index = (cursor + n) % channels_.size();
                if (auto channel = channels_[index].lock()) {
                    std::lock_guard slot(channel->mutex);
                    if (channel->pending.empty()) continue;
                    frame.swap(channel->pending);
                    width = channel->width; height = channel->height; stride = channel->stride;
                    selected = std::move(channel); cursor = index + 1; break;
                }
            }
            if (!selected) { wake_.wait_for(lock, std::chrono::milliseconds(50)); continue; }
        }
        try {
            if (!infer_ && !detector) { detector = std::make_unique<PersonDetector>(root_); ++sessions_; }
            auto result = infer_ ? infer_(frame.data(), width, height, stride) : detector->infer(frame.data(), width, height, stride);
            ++inferred_;
            std::lock_guard lock(selected->mutex);
            selected->result = std::move(result);
        } catch (const std::exception&) {
            // Do not put provider/driver diagnostics containing local paths into media state.
            std::lock_guard lock(selected->mutex);
            selected->error = "Shared CUDA inference unavailable; automatic detection stopped";
        }
    }
}
}
