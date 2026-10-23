#include "platform/local_pipe.hpp"
#include "platform/credentials.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QTextStream>
#include <QLocalSocket>
#include <QJsonDocument>
#include <QFile>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>

#include <stdexcept>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    try {
        opennvr::LocalPipeServer server(opennvr::engine_pipe_name(directory.path()), opennvr::current_user_sid(),
            [](const QJsonObject& request) { return QJsonObject{{"ok",true},{"echo",request["command"]}}; });
        for (int i = 0; i < 5; ++i) {
            const auto reply = opennvr::engine_request(directory.path(), {{"command","status"}});
            if (!reply["ok"].toBool() || reply["echo"].toString() != "status") throw std::runtime_error("Pipe response lost or disconnected too early");
        }
        bool duplicate_rejected = false;
        try {
            opennvr::LocalPipeServer duplicate(opennvr::engine_pipe_name(directory.path()), opennvr::current_user_sid(),
                [](const QJsonObject&) { return QJsonObject{}; });
        } catch (const std::exception&) { duplicate_rejected = true; }
        if (!duplicate_rejected) throw std::runtime_error("Duplicate pipe server was accepted");
        {
            QLocalSocket socket;
            socket.connectToServer(opennvr::engine_pipe_name(directory.path()));
            if (!socket.waitForConnected(2000)) throw std::runtime_error("Cannot connect malformed request test");
            socket.write("{\"v\":2,\"command\":\"status\"}\n");
            socket.waitForBytesWritten(2000);
            QByteArray reply;
            while (!reply.contains('\n') && socket.waitForReadyRead(2000)) reply += socket.readAll();
            if (QJsonDocument::fromJson(reply).object()["ok"].toBool(true)) throw std::runtime_error("Unsupported IPC version was accepted");
            socket.write("\n"); socket.waitForBytesWritten(1000);
        }
        const auto sid = opennvr::current_user_sid();
        const auto set_acl = [&](const QString& path, bool everyone, bool readonly_config = false) {
            const auto sddl = QString("D:P(A;;%1;;;%2)(A;;FA;;;SY)(A;;FA;;;BA)(A;;%3;;;LS)%4")
                .arg(readonly_config ? "FRFX" : "FA", sid, readonly_config ? "FRFX" : "0x1301bf", everyone ? "(A;;FR;;;WD)" : "");
            PSECURITY_DESCRIPTOR descriptor = nullptr;
            if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(reinterpret_cast<LPCWSTR>(sddl.utf16()), SDDL_REVISION_1, &descriptor, nullptr))
                throw std::runtime_error("Cannot construct test ACL");
            const bool ok = SetFileSecurityW(reinterpret_cast<LPCWSTR>(path.utf16()),
                DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, descriptor);
            LocalFree(descriptor);
            if (!ok) throw std::runtime_error("Cannot restrict isolated test directory");
        };
        set_acl(directory.path(), false);
        opennvr::require_service_data_acl(directory.path(), sid);
        const auto credential_path = directory.path() + "/test-camera.json";
        opennvr::save_camera_credential(credential_path, {"127.0.0.1","unit-test","unit-test-value"}, true);
        if (opennvr::load_camera_credential(credential_path).password != "unit-test-value")
            throw std::runtime_error("Machine DPAPI round trip failed");
        QFile encrypted(credential_path);
        if (!encrypted.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot inspect encrypted test credential");
        if (encrypted.readAll().contains("unit-test-value")) throw std::runtime_error("Plaintext credential written");
        encrypted.close();
        const auto config = directory.path() + "/config";
        if (!QDir().mkpath(config)) throw std::runtime_error("Cannot create config test directory");
        set_acl(config, false, true);
        opennvr::require_service_data_acl(config, sid, true);
        QFile blocked(config + "/must-not-write.txt");
        HANDLE token = nullptr; TOKEN_ELEVATION elevation{}; DWORD token_size = 0;
        const bool token_open = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token);
        const bool administrator = token_open && GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &token_size) && elevation.TokenIsElevated;
        if (token) CloseHandle(token);
        if (blocked.open(QIODevice::WriteOnly) && !administrator) throw std::runtime_error("Operator can write protected config");
        blocked.close();
        set_acl(config, false);
        bool config_rejected = false;
        try { opennvr::require_service_data_acl(config, sid, true); } catch (const std::exception&) { config_rejected = true; }
        if (!config_rejected) throw std::runtime_error("Writable service config was accepted");
        set_acl(directory.path(), true);
        bool acl_rejected = false;
        try { opennvr::require_service_data_acl(directory.path(), sid); } catch (const std::exception&) { acl_rejected = true; }
        if (!acl_rejected) throw std::runtime_error("Broad data ACL was accepted");
        set_acl(directory.path(), false);
        QTextStream(stdout) << "PASS repeated IPC delivery and duplicate-instance protection" << Qt::endl;
        QTextStream(stdout) << "PASS IPC version rejection, protected ACL validation and DPAPI without plaintext" << Qt::endl;
        return 0;
    } catch (const std::exception& error) { QTextStream(stderr) << error.what() << Qt::endl; return 1; }
}
