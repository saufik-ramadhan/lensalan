#pragma once

#include "storage/event_store.hpp"
#include <QJsonObject>
#include <QString>
#include <memory>

namespace opennvr {
struct EngineOptions {
    QString root;
    QString data;
    QString credential;
    QString operator_sid;
    bool synthetic = false;
    bool service = false;
    int run_seconds = 0;
};

class EngineHost {
public:
    explicit EngineHost(EngineOptions options);
    ~EngineHost();
    void start();
    void stop();
    bool stop_requested() const;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace opennvr
