#pragma once
#include "media/encoded_ring.hpp"
#include <QJsonObject>
#include <QString>
#include <QUuid>
#include <deque>
#include <mutex>

namespace opennvr {
class LiveHub {
public:
    void reset();
    void publish(const EncodedPacket& packet);
    QJsonObject read(const QJsonObject& request);
private:
    struct Entry { qint64 sequence; EncodedPacket packet; };
    std::mutex mutex_;
    std::deque<Entry> packets_;
    std::size_t bytes_ = 0;
    qint64 sequence_ = 0;
    qint64 generation_ = 0;
    QString epoch_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    std::string caps_;
};
}
