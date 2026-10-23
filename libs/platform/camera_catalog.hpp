#pragma once
#include <QJsonObject>
#include <QString>
#include <vector>

namespace opennvr {
struct CameraSource { QString id; QString name; QString credential; };
std::vector<CameraSource> camera_sources(const QJsonObject& settings, const QString& directory, const QString& legacy);
QJsonObject read_camera_catalog(const QString& file);
}
