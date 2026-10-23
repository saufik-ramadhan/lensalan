#include "media/event_writer.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace opennvr {

EventWriter::EventWriter(const std::filesystem::path& path, const EncodedPacket& first)
    : generation_(first.generation), caps_(first.caps) {
    if (!first.keyframe || first.caps.empty() || !first.data) {
        throw std::invalid_argument("Recording must start on a configured keyframe");
    }
    base_ = first.pts;
    if (GST_CLOCK_TIME_IS_VALID(first.dts)) base_ = std::min(base_, first.dts);
    if (!GST_CLOCK_TIME_IS_VALID(base_)) base_ = 0;

    GError* error = nullptr;
    pipeline_ = gst_parse_launch("appsrc name=input format=time is-live=false block=true "
        "max-bytes=8388608 ! h264parse ! matroskamux ! filesink name=file", &error);
    if (error || !pipeline_) {
        if (error) g_error_free(error);
        close();
        throw std::runtime_error("Cannot construct recording pipeline");
    }
    source_ = GST_APP_SRC(gst_bin_get_by_name(GST_BIN(pipeline_), "input"));
    GstCaps* caps = gst_caps_from_string(first.caps.c_str());
    gst_app_src_set_caps(source_, caps);
    gst_caps_unref(caps);
    GstElement* sink = gst_bin_get_by_name(GST_BIN(pipeline_), "file");
    const auto utf8 = path.u8string();
    g_object_set(sink, "location", reinterpret_cast<const char*>(utf8.c_str()), nullptr);
    gst_object_unref(sink);
    if (gst_element_set_state(pipeline_, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        close();
        throw std::runtime_error("Cannot start recording pipeline");
    }
}

EventWriter::~EventWriter() { close(); }

void EventWriter::push(const EncodedPacket& packet) {
    if (!source_ || packet.generation != generation_ || packet.caps != caps_) {
        throw std::runtime_error("Recording stream discontinuity");
    }
    GstBuffer* buffer = gst_buffer_new_allocate(nullptr, packet.data->size(), nullptr);
    gst_buffer_fill(buffer, 0, packet.data->data(), packet.data->size());
    GST_BUFFER_PTS(buffer) = GST_CLOCK_TIME_IS_VALID(packet.pts) ? packet.pts - std::min(base_, packet.pts) : GST_CLOCK_TIME_NONE;
    GST_BUFFER_DTS(buffer) = GST_CLOCK_TIME_IS_VALID(packet.dts) ? packet.dts - std::min(base_, packet.dts) : GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION(buffer) = packet.duration;
    if (!packet.keyframe) GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT);
    if (gst_app_src_push_buffer(source_, buffer) != GST_FLOW_OK) {
        throw std::runtime_error("Recording writer rejected a packet");
    }
    ++packets_;
}

void EventWriter::finish() {
    if (!pipeline_) return;
    gst_app_src_end_of_stream(source_);
    GstBus* bus = gst_element_get_bus(pipeline_);
    GstMessage* message = gst_bus_timed_pop_filtered(bus, 10 * GST_SECOND,
        static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
    const bool success = message && GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS;
    if (message) gst_message_unref(message);
    gst_object_unref(bus);
    close();
    if (!success) throw std::runtime_error("Recording finalization failed; file may be partial");
}

void EventWriter::close() noexcept {
    if (pipeline_) gst_element_set_state(pipeline_, GST_STATE_NULL);
    if (source_) gst_object_unref(source_);
    if (pipeline_) gst_object_unref(pipeline_);
    source_ = nullptr;
    pipeline_ = nullptr;
}

} // namespace opennvr
