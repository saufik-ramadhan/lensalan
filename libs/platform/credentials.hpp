#pragma once

#include <QString>

namespace opennvr {

struct CameraCredential {
    QString host;
    QString username;
    QString password;
};

CameraCredential load_camera_credential(const QString& file);
void save_camera_credential(const QString& file, const CameraCredential& credential, bool machine_scope = false);

} // namespace opennvr
