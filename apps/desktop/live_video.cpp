#include "live_video.hpp"
#include "platform/local_pipe.hpp"
#include "media/probe.hpp"
#include <QJsonArray>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>

namespace {
struct Decoder {
    GstElement* pipeline = nullptr;
    GstElement* source = nullptr;
    GstElement* sink = nullptr;
    ~Decoder() {
        if (pipeline) gst_element_set_state(pipeline, GST_STATE_NULL);
        if (source) gst_object_unref(source);
        if (sink) gst_object_unref(sink);
        if (pipeline) gst_object_unref(pipeline);
    }
    explicit Decoder(const QString& caps) {
        GError* error = nullptr;
        pipeline = gst_parse_launch("appsrc name=input is-live=true format=time block=false max-bytes=524288 "
            "! h264parse ! d3d11h264dec ! d3d11download ! videoconvert ! video/x-raw,format=BGRA "
            "! appsink name=frames max-buffers=1 drop=true sync=false", &error);
        if (error) g_error_free(error);
        if (!pipeline || error) return;
        source = gst_bin_get_by_name(GST_BIN(pipeline), "input");
        sink = gst_bin_get_by_name(GST_BIN(pipeline), "frames");
        auto* format = gst_caps_from_string(caps.toUtf8().constData());
        if (!format) { gst_object_unref(source); source = nullptr; return; }
        gst_app_src_set_caps(GST_APP_SRC(source), format); gst_caps_unref(format);
        if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
            gst_object_unref(source); source = nullptr;
        }
    }
    bool push(const QJsonArray& frames) {
        if (!source) return false;
        guint64 queued = 0; g_object_get(source, "current-level-bytes", &queued, nullptr);
        if (queued > 524288) return false;
        for (const auto& item : frames) {
            const auto frame = item.toObject();
            const auto data = QByteArray::fromBase64(frame["data"].toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
            if (data.isEmpty() || data.size() > 192 * 1024) return false;
            auto* buffer = gst_buffer_new_allocate(nullptr, data.size(), nullptr);
            gst_buffer_fill(buffer, 0, data.constData(), data.size());
            GST_BUFFER_PTS(buffer) = frame["pts"].toString().toULongLong();
            GST_BUFFER_DTS(buffer) = frame["dts"].toString().toULongLong();
            GST_BUFFER_DURATION(buffer) = frame["duration"].toString().toULongLong();
            if (!frame["key"].toBool()) GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT);
            if (gst_app_src_push_buffer(GST_APP_SRC(source), buffer) != GST_FLOW_OK) return false;
        }
        return true;
    }
    QImage pull() {
        if (!sink) return {};
        auto* sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 0);
        if (!sample) return {};
        GstVideoInfo info;
        GstVideoFrame frame{};
        QImage image;
        if (gst_video_info_from_caps(&info, gst_sample_get_caps(sample)) &&
            gst_video_frame_map(&frame, &info, gst_sample_get_buffer(sample), GST_MAP_READ)) {
            image = QImage(static_cast<const uchar*>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0)),
                GST_VIDEO_FRAME_WIDTH(&frame), GST_VIDEO_FRAME_HEIGHT(&frame), GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0), QImage::Format_ARGB32).copy();
            gst_video_frame_unmap(&frame);
        }
        gst_sample_unref(sample);
        return image;
    }
};
}
LiveVideo::LiveVideo(QQuickItem* parent) : QQuickItem(parent) { setFlag(ItemHasContents, true); }
LiveVideo::~LiveVideo() { stopping_ = true; if (worker_.joinable()) worker_.join(); }
void LiveVideo::setDirectory(QString value) { { std::lock_guard lock(mutex_); if (directory_ == value) return; directory_ = std::move(value); } emit sourceChanged(); }
void LiveVideo::setCameraId(QString value) { { std::lock_guard lock(mutex_); if (camera_id_ == value) return; camera_id_ = std::move(value); } emit sourceChanged(); }
void LiveVideo::setActive(bool value) { { std::lock_guard lock(mutex_); if (active_ == value) return; active_ = value; } emit sourceChanged(); }
void LiveVideo::componentComplete() { QQuickItem::componentComplete(); worker_ = std::jthread([this] { run(); }); }
void LiveVideo::deliver(QImage image, QString status) {
    QMetaObject::invokeMethod(this, [this, image = std::move(image), status = std::move(status)] {
        { std::lock_guard lock(mutex_); image_ = image; }
        if (!image.isNull()) ++frames_;
        status_ = status; emit statusChanged(); update();
    }, Qt::QueuedConnection);
}
void LiveVideo::run() {
    opennvr::initialize_media();
    QString directory, id, epoch;
    qint64 cursor = 0, generation = -1;
    std::unique_ptr<Decoder> decoder;
    auto last_frame = std::chrono::steady_clock::now();
    auto displayed = last_frame;
    bool blank = false;
    while (!stopping_) {
        QString next_directory, next_id; bool enabled;
        { std::lock_guard lock(mutex_); next_directory = directory_; next_id = camera_id_; enabled = active_; }
        if (!enabled || next_directory.isEmpty() || next_id.isEmpty()) {
            decoder.reset(); cursor = 0; generation = -1;
            if (!blank) { deliver({}, "Live tidak aktif"); blank = true; }
            std::this_thread::sleep_for(std::chrono::milliseconds(80)); continue;
        }
        if (next_directory != directory || next_id != id) {
            decoder.reset(); cursor = 0; generation = -1; directory = next_directory; id = next_id;
            deliver({}, "Menghubungkan video");
        }
        const auto reply = opennvr::engine_request(directory, {{"command","live"},{"cursor",cursor},{"generation",generation},{"epoch",epoch}}, 500, ".Media." + id);
        if (!reply["ok"].toBool()) {
            decoder.reset(); cursor = 0; generation = -1;
            if (!blank) { deliver({}, "Kanal live belum tersedia"); blank = true; }
            std::this_thread::sleep_for(std::chrono::milliseconds(300)); continue;
        }
        if (reply["epoch"].toString() != epoch || reply["generation"].toInteger() != generation || reply["reset"].toBool()) {
            decoder.reset(); generation = reply["generation"].toInteger(); epoch = reply["epoch"].toString();
            if (!blank) { deliver({}, "Menunggu keyframe"); blank = true; }
        }
        const auto frames = reply["frames"].toArray();
        if (!decoder && !frames.isEmpty()) decoder = std::make_unique<Decoder>(reply["caps"].toString());
        cursor = reply["cursor"].toInteger();
        if (decoder && !decoder->push(frames)) {
            decoder.reset(); cursor = 0; generation = -1; deliver({}, "Decoder video memulai ulang");
        }
        const auto now = std::chrono::steady_clock::now();
        if (decoder && now - displayed >= std::chrono::milliseconds(100)) {
            auto image = decoder->pull();
            if (!image.isNull()) { last_frame = displayed = now; blank = false; deliver(std::move(image), "Live"); }
        }
        if (now - last_frame > std::chrono::seconds(3) && !blank) { deliver({}, "Video terhenti; menunggu frame"); blank = true; }
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
}
QSGNode* LiveVideo::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
    QImage image;
    { std::lock_guard lock(mutex_); image = image_; }
    if (image.isNull() || !window()) { delete old; return nullptr; }
    auto* node = static_cast<QSGSimpleTextureNode*>(old);
    if (!node) { node = new QSGSimpleTextureNode; node->setOwnsTexture(true); }
    node->setTexture(window()->createTextureFromImage(image));
    const auto fitted = QSizeF(image.size()).scaled(QSizeF(width(), height()), Qt::KeepAspectRatio);
    node->setRect(QRectF((width() - fitted.width()) / 2, (height() - fitted.height()) / 2, fitted.width(), fitted.height()));
    return node;
}
