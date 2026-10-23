#include "detection/person_stream.hpp"

#include <gst/app/gstappsink.h>
#include <gst/video/video.h>

#include <stdexcept>

namespace opennvr {
namespace {
struct PipelineDelete {
    void operator()(GstElement* value) const {
        if (value) { gst_element_set_state(value, GST_STATE_NULL); gst_object_unref(value); }
    }
};
struct ElementDelete { void operator()(GstElement* value) const { if (value) gst_object_unref(value); } };
struct BusDelete { void operator()(GstBus* value) const { if (value) gst_object_unref(value); } };
struct SampleDelete { void operator()(GstSample* value) const { if (value) gst_sample_unref(value); } };
}

class PersonStream::Impl {
public:
    Impl(const std::filesystem::path& root, const std::string& host,
         const std::string& username, const std::string& password) : detector_(root) {
        GError* error = nullptr;
        pipeline_.reset(gst_parse_launch("rtspsrc name=camera latency=150 protocols=tcp tcp-timeout=5000000 "
            "! application/x-rtp,media=video,encoding-name=H264 ! rtph264depay ! h264parse "
            "! d3d11h264dec ! d3d11download ! videoconvert ! video/x-raw,format=BGR "
            "! appsink name=frames max-buffers=1 drop=true sync=false", &error));
        if (error || !pipeline_) {
            if (error) g_error_free(error);
            throw std::runtime_error("AI decode pipeline could not be created");
        }
        std::unique_ptr<GstElement, ElementDelete> camera(gst_bin_get_by_name(GST_BIN(pipeline_.get()), "camera"));
        const auto url = "rtsp://" + host + ":554/stream2";
        g_object_set(camera.get(), "location", url.c_str(), "user-id", username.c_str(),
                     "user-pw", password.c_str(), nullptr);
        sink_.reset(gst_bin_get_by_name(GST_BIN(pipeline_.get()), "frames"));
        bus_.reset(gst_element_get_bus(pipeline_.get()));
        if (gst_element_set_state(pipeline_.get(), GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
            throw std::runtime_error("AI substream could not start");
        }
    }

    std::optional<DetectionResult> poll() {
        if (GstMessage* message = gst_bus_pop_filtered(bus_.get(),
            static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS))) {
            gst_message_unref(message);
            throw std::runtime_error("AI substream unavailable; automatic event detection stopped");
        }
        std::unique_ptr<GstSample, SampleDelete> sample(gst_app_sink_try_pull_sample(GST_APP_SINK(sink_.get()), 0));
        if (!sample) return {};
        GstVideoInfo info;
        if (!gst_video_info_from_caps(&info, gst_sample_get_caps(sample.get()))) {
            throw std::runtime_error("Cannot read AI frame format");
        }
        GstVideoFrame frame{};
        if (!gst_video_frame_map(&frame, &info, gst_sample_get_buffer(sample.get()), GST_MAP_READ)) {
            throw std::runtime_error("Cannot map AI frame");
        }
        try {
            auto result = detector_.infer(static_cast<const std::uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0)),
                GST_VIDEO_FRAME_WIDTH(&frame), GST_VIDEO_FRAME_HEIGHT(&frame), GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0));
            gst_video_frame_unmap(&frame);
            return result;
        } catch (...) {
            gst_video_frame_unmap(&frame);
            throw;
        }
    }

private:
    PersonDetector detector_;
    std::unique_ptr<GstElement, PipelineDelete> pipeline_;
    std::unique_ptr<GstElement, ElementDelete> sink_;
    std::unique_ptr<GstBus, BusDelete> bus_;
};

PersonStream::PersonStream(const std::filesystem::path& root, const std::string& host,
                         const std::string& username, const std::string& password)
    : impl_(std::make_unique<Impl>(root, host, username, password)) {}
PersonStream::~PersonStream() = default;
std::optional<DetectionResult> PersonStream::poll() { return impl_->poll(); }

} // namespace opennvr
