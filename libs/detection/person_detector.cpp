#include "detection/person_detector.hpp"

#ifdef OPENNVR_HAS_ONNX
#if defined(__MINGW32__) && !defined(_stdcall)
// ORT's Windows C API spells this MSVC calling convention with one underscore.
#define _stdcall __stdcall
#endif
#define ORT_API_MANUAL_INIT
#include <onnxruntime_cxx_api.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <mutex>
#include <stdexcept>

namespace opennvr {
namespace {
void load_runtime(const std::filesystem::path& root) {
    static std::once_flag once;
    std::call_once(once, [&] {
        const auto deps = root / ".deps";
        const auto library = deps / "onnxruntime-win-x64-gpu-1.23.2" / "lib";
        std::wstring runtime_path;
        for (const auto& base : {library,
            deps / "cuda/nvidia-cublas-cu12/nvidia/cublas/bin",
            deps / "cuda/nvidia-cudnn-cu12/nvidia/cudnn/bin",
            deps / "cuda/nvidia-cuda-runtime-cu12/nvidia/cuda_runtime/bin",
            deps / "cuda/nvidia-cufft-cu12/nvidia/cufft/bin"}) {
            if (std::filesystem::exists(base)) {
                AddDllDirectory(base.c_str());
                runtime_path += base.wstring() + L";";
            }
        }
        // ORT's provider loader uses the legacy Windows DLL search order, which
        // does not consult AddDllDirectory. This changes only this process's PATH.
        const auto old_size = GetEnvironmentVariableW(L"PATH", nullptr, 0);
        std::wstring old_path(old_size, L'\0');
        if (old_size) {
            GetEnvironmentVariableW(L"PATH", old_path.data(), old_size);
            old_path.resize(old_size - 1);
        }
        runtime_path += old_path;
        SetEnvironmentVariableW(L"PATH", runtime_path.c_str());
        HMODULE module = LoadLibraryExW((library / "onnxruntime.dll").c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!module) throw std::runtime_error("ONNX Runtime tidak dapat dimuat. Jalankan tools/bootstrap_ai.py.");
        const auto entry = reinterpret_cast<const OrtApiBase* (ORT_API_CALL*)()>(GetProcAddress(module, "OrtGetApiBase"));
        if (!entry) throw std::runtime_error("ONNX Runtime C API tidak tersedia.");
        const auto* api = entry()->GetApi(ORT_API_VERSION);
        if (!api) throw std::runtime_error("Versi ONNX Runtime tidak cocok dengan build.");
        Ort::InitApi(api);
        // Runtime and DLL directory handles intentionally live until process exit.
    });
}

float iou(const PersonBox& a, const PersonBox& b) {
    const auto width = std::max(0.F, std::min(a.x + a.width, b.x + b.width) - std::max(a.x, b.x));
    const auto height = std::max(0.F, std::min(a.y + a.height, b.y + b.height) - std::max(a.y, b.y));
    const auto intersection = width * height;
    const auto total = a.width * a.height + b.width * b.height - intersection;
    return total > 0 ? intersection / total : 0;
}
} // namespace

class PersonDetector::Impl {
public:
    Impl(const std::filesystem::path& root, float threshold) : threshold_(threshold) {
        load_runtime(root);
        env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_ERROR, "OpenNVR");
        env_->DisableTelemetryEvents();
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(2);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        OrtCUDAProviderOptions cuda{};
        cuda.device_id = 0;
        cuda.cudnn_conv_algo_search = OrtCudnnConvAlgoSearchHeuristic;
        cuda.do_copy_in_default_stream = 1;
        cuda.gpu_mem_limit = 1024ULL * 1024 * 1024;
        cuda.arena_extend_strategy = 1;
        options.AppendExecutionProvider_CUDA(cuda); // A failure is surfaced; no silent CPU session.
        const auto model = root / ".deps/models/yolox_tiny.onnx";
        session_ = std::make_unique<Ort::Session>(*env_, model.c_str(), options);
        Ort::AllocatorWithDefaultOptions allocator;
        const auto shape = session_->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        if (shape != std::vector<int64_t>{1, 3, 416, 416}) {
            throw std::runtime_error("Model tidak cocok: diharapkan YOLOX-Tiny 1x3x416x416.");
        }
        const auto input = session_->GetInputNameAllocated(0, allocator);
        const auto output = session_->GetOutputNameAllocated(0, allocator);
        input_name_ = input.get();
        output_name_ = output.get();
        tensor_.resize(3 * side_ * side_, 114.F);
        std::vector<std::uint8_t> blank(32 * 32 * 3, 114);
        infer(blank.data(), 32, 32, 32 * 3); // Warm up before enabling detection.
    }

    DetectionResult infer(const std::uint8_t* bgr, int width, int height, int stride) {
        if (!bgr || width <= 0 || height <= 0 || stride < width * 3) {
            throw std::invalid_argument("Invalid BGR frame");
        }
        const auto began = std::chrono::steady_clock::now();
        std::fill(tensor_.begin(), tensor_.end(), 114.F);
        const float scale = std::min(static_cast<float>(side_) / width, static_cast<float>(side_) / height);
        const int resized_w = std::max(1, static_cast<int>(width * scale));
        const int resized_h = std::max(1, static_cast<int>(height * scale));
        // YOLOX uses BGR 0..255, CHW, top-left alignment, and a 114-valued letterbox.
        for (int y = 0; y < resized_h; ++y) {
            const float source_y = std::clamp((y + 0.5F) * height / resized_h - 0.5F, 0.F, static_cast<float>(height - 1));
            const int y0 = static_cast<int>(source_y), y1 = std::min(y0 + 1, height - 1);
            const float fy = source_y - y0;
            for (int x = 0; x < resized_w; ++x) {
                const float source_x = std::clamp((x + 0.5F) * width / resized_w - 0.5F, 0.F, static_cast<float>(width - 1));
                const int x0 = static_cast<int>(source_x), x1 = std::min(x0 + 1, width - 1);
                const float fx = source_x - x0;
                for (int channel = 0; channel < 3; ++channel) {
                    const auto top = bgr[y0 * stride + x0 * 3 + channel] * (1 - fx) + bgr[y0 * stride + x1 * 3 + channel] * fx;
                    const auto bottom = bgr[y1 * stride + x0 * 3 + channel] * (1 - fx) + bgr[y1 * stride + x1 * 3 + channel] * fx;
                    tensor_[channel * side_ * side_ + y * side_ + x] = std::round(top * (1 - fy) + bottom * fy);
                }
            }
        }
        auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const std::array<int64_t, 4> shape{1, 3, side_, side_};
        auto input = Ort::Value::CreateTensor<float>(memory, tensor_.data(), tensor_.size(), shape.data(), shape.size());
        const char* input_names[]{input_name_.c_str()};
        const char* output_names[]{output_name_.c_str()};
        auto output = session_->Run(Ort::RunOptions{nullptr}, input_names, &input, 1, output_names, 1);
        const auto info = output[0].GetTensorTypeAndShapeInfo();
        const auto output_shape = info.GetShape();
        if (output_shape != std::vector<int64_t>{1, 3549, 85}) {
            throw std::runtime_error("Unsupported YOLOX output shape");
        }
        const float* raw = output[0].GetTensorData<float>();
        std::vector<PersonBox> candidates;
        std::size_t anchor = 0;
        for (const int cell_stride : {8, 16, 32}) {
            const int cells = side_ / cell_stride;
            for (int y = 0; y < cells; ++y) for (int x = 0; x < cells; ++x, ++anchor) {
                const auto* prediction = raw + anchor * 85;
                const float confidence = prediction[4] * prediction[5]; // COCO class 0 = person.
                if (!std::isfinite(confidence) || confidence < threshold_) continue;
                const float center_x = (prediction[0] + x) * cell_stride / scale;
                const float center_y = (prediction[1] + y) * cell_stride / scale;
                const float box_w = std::exp(std::clamp(prediction[2], -20.F, 20.F)) * cell_stride / scale;
                const float box_h = std::exp(std::clamp(prediction[3], -20.F, 20.F)) * cell_stride / scale;
                const auto left = std::clamp(center_x - box_w / 2, 0.F, static_cast<float>(width));
                const auto top = std::clamp(center_y - box_h / 2, 0.F, static_cast<float>(height));
                const auto right = std::clamp(center_x + box_w / 2, 0.F, static_cast<float>(width));
                const auto bottom = std::clamp(center_y + box_h / 2, 0.F, static_cast<float>(height));
                if (right > left && bottom > top) candidates.push_back({left, top, right - left, bottom - top, confidence});
            }
        }
        std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) { return a.confidence > b.confidence; });
        DetectionResult result;
        for (const auto& candidate : candidates) {
            if (std::none_of(result.people.begin(), result.people.end(), [&](const auto& kept) { return iou(candidate, kept) > 0.45F; })) {
                result.people.push_back(candidate);
            }
        }
        result.inference_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
        return result;
    }

    const std::string provider_{"CUDAExecutionProvider / YOLOX-Tiny 416"};
private:
    static constexpr int side_ = 416;
    float threshold_;
    std::unique_ptr<Ort::Env> env_;
    std::unique_ptr<Ort::Session> session_;
    std::string input_name_;
    std::string output_name_;
    std::vector<float> tensor_;
};

PersonDetector::PersonDetector(const std::filesystem::path& root, float confidence)
    : impl_(std::make_unique<Impl>(root, confidence)) {}
PersonDetector::~PersonDetector() = default;
DetectionResult PersonDetector::infer(const std::uint8_t* data, int width, int height, int stride) {
    return impl_->infer(data, width, height, stride);
}
const std::string& PersonDetector::provider() const { return impl_->provider_; }

} // namespace opennvr
#else
#include <stdexcept>
namespace opennvr {
class PersonDetector::Impl {};
PersonDetector::PersonDetector(const std::filesystem::path&, float) {
    throw std::runtime_error("Build ini belum menyertakan ONNX Runtime. Jalankan bootstrap_ai.py lalu build ulang.");
}
PersonDetector::~PersonDetector() = default;
DetectionResult PersonDetector::infer(const std::uint8_t*, int, int, int) { throw std::runtime_error("AI unavailable"); }
const std::string& PersonDetector::provider() const { static const std::string none = "unavailable"; return none; }
}
#endif
