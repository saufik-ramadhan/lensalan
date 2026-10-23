#include "engine/live_hub.hpp"
#include <QJsonArray>
#include <QDateTime>

namespace opennvr {
void LiveHub::reset() {
    std::lock_guard lock(mutex_);
    packets_.clear(); bytes_ = 0; caps_.clear(); ++generation_;
}
void LiveHub::publish(const EncodedPacket& packet) {
    std::lock_guard lock(mutex_);
    if (packet.caps != caps_ || packet.data->size() > 192 * 1024) {
        packets_.clear(); bytes_ = 0; caps_ = packet.caps; ++generation_;
    }
    if (packet.data->size() > 192 * 1024 || (packets_.empty() && !packet.keyframe)) return;
    packets_.push_back({++sequence_, packet}); bytes_ += packet.data->size();
    while (bytes_ > 2 * 1024 * 1024 || packets_.size() > 180) {
        bytes_ -= packets_.front().packet.data->size(); packets_.pop_front();
        while (!packets_.empty() && !packets_.front().packet.keyframe) {
            bytes_ -= packets_.front().packet.data->size(); packets_.pop_front();
        }
    }
}
QJsonObject LiveHub::read(const QJsonObject& request) {
    std::vector<Entry> batch;
    qint64 cursor = request["cursor"].toInteger(0), generation;
    bool reset = false;
    std::string caps;
    {
        std::lock_guard lock(mutex_);
        generation = generation_; caps = caps_;
        if (!packets_.empty()) {
            reset = request["epoch"].toString() != epoch_ || request["generation"].toInteger(-1) != generation_ || cursor == 0 ||
                cursor < packets_.front().sequence - 1 || cursor > packets_.back().sequence;
            if (reset) {
                for (auto i = packets_.rbegin(); i != packets_.rend(); ++i) {
                    if (i->packet.keyframe) { cursor = i->sequence - 1; break; }
                }
            }
            std::size_t bytes = 0;
            for (const auto& entry : packets_) {
                if (entry.sequence <= cursor) continue;
                if (bytes + entry.packet.data->size() > 192 * 1024) break;
                batch.push_back(entry); bytes += entry.packet.data->size(); cursor = entry.sequence;
            }
        }
    }
    QJsonArray frames;
    for (const auto& entry : batch) {
        const auto& packet = entry.packet;
        const auto encoded = QByteArray(reinterpret_cast<const char*>(packet.data->data()), packet.data->size()).toBase64();
        frames.append(QJsonObject{{"data",QString::fromLatin1(encoded)}, {"key",packet.keyframe},
            {"pts",QString::number(packet.pts)}, {"dts",QString::number(packet.dts)}, {"duration",QString::number(packet.duration)}});
    }
    return {{"ok",true},{"reset",reset},{"epoch",epoch_},{"generation",generation},{"cursor",cursor},
        {"caps",QString::fromStdString(caps)},{"frames",frames}};
}
}
