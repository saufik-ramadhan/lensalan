#include "engine/host.hpp"
#include "platform/local_pipe.hpp"
#include "platform/credentials.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTextStream>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace {
std::atomic_bool stop_signal{false};
SERVICE_STATUS_HANDLE service_handle = nullptr;
opennvr::EngineOptions options;

void service_status(DWORD state, DWORD error = NO_ERROR) {
    SERVICE_STATUS status{};
    status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    status.dwCurrentState = state;
    status.dwControlsAccepted = state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
    status.dwWin32ExitCode = error;
    status.dwWaitHint = state == SERVICE_START_PENDING || state == SERVICE_STOP_PENDING ? 45000 : 0;
    status.dwCheckPoint = status.dwWaitHint ? 1 : 0;
    if (service_handle) SetServiceStatus(service_handle, &status);
}
DWORD WINAPI control(DWORD code, DWORD, void*, void*) {
    if (code == SERVICE_CONTROL_STOP || code == SERVICE_CONTROL_SHUTDOWN) {
        stop_signal = true;
        service_status(SERVICE_STOP_PENDING);
    }
    return NO_ERROR;
}
BOOL WINAPI console_control(DWORD code) {
    if (code == CTRL_C_EVENT || code == CTRL_BREAK_EVENT || code == CTRL_CLOSE_EVENT) { stop_signal = true; return TRUE; }
    return FALSE;
}
int run() {
    try {
        opennvr::EngineHost host(options);
        host.start();
        if (options.service) service_status(SERVICE_RUNNING);
        while (!stop_signal && !host.stop_requested()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (options.service) service_status(SERVICE_STOP_PENDING);
        host.stop();
        return 0;
    } catch (const std::exception& error) {
        const QJsonObject report{{"ok",false},{"error",QString::fromUtf8(error.what())},
            {"timeMs",QDateTime::currentMSecsSinceEpoch()}};
        QTextStream(stderr) << QJsonDocument(report).toJson(QJsonDocument::Compact) << Qt::endl;
        QSaveFile file(options.data + "/last-startup-error.json");
        if (file.open(QIODevice::WriteOnly)) { file.write(QJsonDocument(report).toJson()); file.commit(); }
        return 1;
    }
}
void WINAPI service_main(DWORD, LPWSTR*) {
    service_handle = RegisterServiceCtrlHandlerExW(L"OpenNVR", control, nullptr);
    if (!service_handle) return;
    service_status(SERVICE_START_PENDING);
    const auto result = run();
    service_status(SERVICE_STOPPED, result == 0 ? NO_ERROR : ERROR_EXCEPTION_IN_SERVICE);
}
bool elevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION value{}; DWORD size = 0;
    const bool result = GetTokenInformation(token, TokenElevation, &value, sizeof(value), &size) && value.TokenIsElevated;
    CloseHandle(token); return result;
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    const auto option = [&](const QString& key, const QString& fallback = {}) {
        const auto i = args.indexOf(key);
        return i >= 0 && i + 1 < args.size() ? args[i + 1] : fallback;
    };
    options.root = QDir(option("--root", QDir::currentPath())).absolutePath();
    options.data = QDir(option("--data", options.root + "/.local/engine")).absolutePath();
    options.credential = option("--credential");
    options.operator_sid = option("--operator-sid");
    options.synthetic = args.contains("--synthetic");
    options.service = args.contains("--service");
    bool seconds_valid = true;
    options.run_seconds = option("--run-seconds", "0").toInt(&seconds_valid);
    if (!seconds_valid || options.run_seconds < 0 || options.run_seconds > 86400 ||
        (options.service && (options.synthetic || options.run_seconds != 0))) {
        QTextStream(stderr) << "Invalid run mode or duration" << Qt::endl; return 2;
    }
    if (args.contains("--request")) {
        QJsonObject request{{"command",option("--request")}};
        if (args.contains("--id")) request["id"] = option("--id");
        const auto response = opennvr::engine_request(options.data, request);
        QTextStream(stdout) << QJsonDocument(response).toJson(QJsonDocument::Compact) << Qt::endl;
        return response["ok"].toBool() ? 0 : 1;
    }
    if (args.contains("--provision-service")) {
        try {
            if (!elevated()) throw std::runtime_error("Service provisioning requires Administrator Windows");
            if (options.operator_sid.isEmpty()) throw std::runtime_error("An explicit operator SID is required");
            // The installer creates and restricts the data directory before this command.
            if (!QFileInfo::exists(options.data + "/.acl-provisioned")) throw std::runtime_error("Run the service installer to provision storage ACLs first");
            opennvr::require_service_data_acl(options.data, options.operator_sid);
            opennvr::require_admin_owned_directory(options.data);
            opennvr::require_admin_owned_directory(options.data + "/config");
            opennvr::require_service_data_acl(options.data + "/config", options.operator_sid, true);
            const auto source = opennvr::load_camera_credential(option("--source-credential", options.root + "/.local/camera.json"));
            opennvr::save_camera_credential(options.data + "/config/camera.json", source, true);
            QSaveFile settings(options.data + "/config/engine.json");
            const QJsonObject data{{"operatorSid",options.operator_sid},{"retentionDays",14},{"quotaGB",100},{"reserveGB",10}};
            if (!settings.open(QIODevice::WriteOnly) || settings.write(QJsonDocument(data).toJson()) < 0 || !settings.commit())
                throw std::runtime_error("Cannot write service settings");
            QTextStream(stdout) << "Service credential provisioned with machine DPAPI" << Qt::endl;
            return 0;
        } catch (const std::exception& error) { QTextStream(stderr) << error.what() << Qt::endl; return 1; }
    }
    // Packaged plugin paths are isolated from the developer's MSYS2 installation.
    if (QFileInfo::exists(options.root + "/lib/gstreamer-1.0")) {
        qputenv("GST_PLUGIN_SYSTEM_PATH", QFile::encodeName(options.root + "/lib/gstreamer-1.0"));
        qputenv("GST_PLUGIN_SYSTEM_PATH_1_0", QFile::encodeName(options.root + "/lib/gstreamer-1.0"));
        qputenv("GST_PLUGIN_PATH", "");
        qputenv("GST_PLUGIN_PATH_1_0", "");
        qputenv("GST_PLUGIN_SCANNER", QFile::encodeName(options.root + "/libexec/gstreamer-1.0/gst-plugin-scanner.exe"));
        qputenv("GST_REGISTRY", QFile::encodeName(options.data + "/gstreamer-registry.bin"));
    }
    if (options.service) {
        SERVICE_TABLE_ENTRYW entries[]{{const_cast<LPWSTR>(L"OpenNVR"),service_main},{nullptr,nullptr}};
        if (!StartServiceCtrlDispatcherW(entries)) { QTextStream(stderr) << "Service dispatcher failed; use --console for a development run" << Qt::endl; return 1; }
        return 0;
    }
    if (!args.contains("--console")) { QTextStream(stdout) << "Use --console, --service, or --request status/events/pause/resume/shutdown" << Qt::endl; return 2; }
    SetConsoleCtrlHandler(console_control, TRUE);
    return run();
}
