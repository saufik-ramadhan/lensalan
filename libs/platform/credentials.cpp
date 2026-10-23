#include "platform/credentials.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#endif

namespace opennvr {

CameraCredential load_camera_credential(const QString& file) {
    QFile source(file);
    if (!source.open(QIODevice::ReadOnly)) throw std::runtime_error("Konfigurasi kamera lokal belum tersedia.");
    const auto json = QJsonDocument::fromJson(source.readAll());
    if (!json.isObject()) throw std::runtime_error("Format konfigurasi kamera tidak valid.");
    const auto object = json.object();
    CameraCredential value{object["host"].toString(), object["username"].toString(), {}};
#ifdef _WIN32
    const auto encoded = object["passwordDpapi"].toString().toLatin1();
    auto bytes = QByteArray::fromBase64(encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (bytes.isEmpty()) throw std::runtime_error("Credential terenkripsi tidak tersedia.");
    DATA_BLOB input{static_cast<DWORD>(bytes.size()), reinterpret_cast<BYTE*>(bytes.data())};
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        throw std::runtime_error("Credential hanya dapat dibaca oleh akun Windows yang menyimpannya.");
    }
    value.password = QString::fromUtf8(reinterpret_cast<const char*>(output.pbData), output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
#else
    throw std::runtime_error("Credential storage memerlukan Windows DPAPI.");
#endif
    if (value.host.isEmpty() || value.username.isEmpty() || value.password.isEmpty()) {
        throw std::runtime_error("Konfigurasi kamera belum lengkap.");
    }
    return value;
}

void save_camera_credential(const QString& file, const CameraCredential& credential) {
#ifdef _WIN32
    auto plaintext = credential.password.toUtf8();
    DATA_BLOB input{static_cast<DWORD>(plaintext.size()), reinterpret_cast<BYTE*>(plaintext.data())};
    DATA_BLOB output{};
    const BOOL ok = CryptProtectData(&input, L"OpenNVR development camera", nullptr, nullptr, nullptr,
                                    CRYPTPROTECT_UI_FORBIDDEN, &output);
    SecureZeroMemory(plaintext.data(), plaintext.size());
    if (!ok) throw std::runtime_error("Windows tidak dapat melindungi credential.");
    const auto encrypted = QByteArray(reinterpret_cast<const char*>(output.pbData), output.cbData).toBase64();
    LocalFree(output.pbData);
    QJsonObject object{{"host", credential.host}, {"username", credential.username},
                       {"passwordDpapi", QString::fromLatin1(encrypted)}};
    if (!QDir().mkpath(QFileInfo(file).absolutePath())) {
        throw std::runtime_error("Folder konfigurasi tidak dapat dibuat.");
    }
    QSaveFile target(file);
    if (!target.open(QIODevice::WriteOnly) ||
        target.write(QJsonDocument(object).toJson()) == -1 || !target.commit()) {
        throw std::runtime_error("Konfigurasi terenkripsi tidak dapat disimpan.");
    }
#else
    Q_UNUSED(file)
    Q_UNUSED(credential)
    throw std::runtime_error("Credential storage memerlukan Windows DPAPI.");
#endif
}

} // namespace opennvr
