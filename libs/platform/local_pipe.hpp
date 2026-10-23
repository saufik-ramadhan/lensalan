#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <functional>
#include <memory>

namespace opennvr {
QString current_user_sid();
void require_service_data_acl(const QString& directory, const QString& operator_sid, bool readonly_config = false);
void require_admin_owned_directory(const QString& directory);
QString engine_pipe_name(const QString& data_directory);
QJsonObject engine_request(const QString& data_directory, const QJsonObject& request, int timeout_ms = 2000, const QString& suffix = {});

class LocalPipeServer {
public:
    LocalPipeServer(const QString& name, const QString& operator_sid,
                   std::function<QJsonObject(const QJsonObject&)> handler);
    ~LocalPipeServer();
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace opennvr
