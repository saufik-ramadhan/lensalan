#include "platform/camera_catalog.hpp"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <stdexcept>

namespace opennvr {
QJsonObject read_camera_catalog(const QString& file) {
    QFile input(file);
    if (!input.open(QIODevice::ReadOnly) || input.size() > 65536) throw std::runtime_error("Cannot read camera catalog");
    const auto document = QJsonDocument::fromJson(input.readAll());
    if (!document.isObject()) throw std::runtime_error("Invalid camera catalog");
    return document.object();
}
std::vector<CameraSource> camera_sources(const QJsonObject& settings, const QString& directory, const QString& legacy) {
    if (!settings.contains("cameras")) return {{"camera-1", "Kamera 1", legacy}};
    const auto list = settings["cameras"].toArray();
    if (!settings["cameras"].isArray() || list.empty() || list.size() > 12) throw std::runtime_error("Camera catalog requires 1 to 12 cameras");
    QSet<QString> ids;
    std::vector<CameraSource> sources;
    for (const auto& item : list) {
        const auto value = item.toObject();
        const auto id = value["id"].toString();
        const auto name = value["name"].toString().trimmed();
        const auto credential = value["credential"].toString();
        if (!QRegularExpression("^[A-Za-z0-9_-]{1,32}$").match(id).hasMatch() || ids.contains(id) ||
            name.isEmpty() || name.size() > 80 || !QRegularExpression("^(camera\\.json|cameras/[A-Za-z0-9_-]{1,32}\\.json)$").match(credential).hasMatch())
            throw std::runtime_error("Invalid or duplicate camera ID, name or credential path");
        ids.insert(id);
        sources.push_back({id, name, QDir(directory).absoluteFilePath(credential)});
    }
    return sources;
}
}
