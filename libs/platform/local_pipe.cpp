#include "platform/local_pipe.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QRegularExpression>
#include <QElapsedTimer>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>
#include <aclapi.h>

#include <array>
#include <algorithm>
#include <stdexcept>
#include <thread>
#include <vector>

namespace opennvr {

QString current_user_sid() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) throw std::runtime_error("Cannot query process identity");
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<std::byte> bytes(size);
    const bool read = GetTokenInformation(token, TokenUser, bytes.data(), size, &size);
    CloseHandle(token);
    if (!read) throw std::runtime_error("Cannot query process SID");
    LPWSTR text = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid, &text))
        throw std::runtime_error("Cannot format process SID");
    const auto sid = QString::fromWCharArray(text);
    LocalFree(text);
    return sid;
}

void require_service_data_acl(const QString& directory, const QString& operator_sid, bool readonly_config) {
    PACL acl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const auto result = GetNamedSecurityInfoW(const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(directory.utf16())),
        SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &acl, nullptr, &descriptor);
    if (result != ERROR_SUCCESS || !descriptor || !acl) {
        if (descriptor) LocalFree(descriptor);
        throw std::runtime_error("Service data directory must have an explicit protected ACL");
    }
    SECURITY_DESCRIPTOR_CONTROL control = 0; DWORD revision = 0;
    bool safe = GetSecurityDescriptorControl(descriptor, &control, &revision) && (control & SE_DACL_PROTECTED);
    for (DWORD i = 0; safe && i < acl->AceCount; ++i) {
        void* raw = nullptr;
        safe = GetAce(acl, i, &raw) && static_cast<ACE_HEADER*>(raw)->AceType == ACCESS_ALLOWED_ACE_TYPE;
        if (!safe) break;
        auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw);
        LPWSTR text = nullptr;
        safe = ConvertSidToStringSidW(&ace->SidStart, &text);
        if (!safe) break;
        const auto sid = QString::fromWCharArray(text);
        LocalFree(text);
        safe = sid == "S-1-5-18" || sid == "S-1-5-32-544" || sid == "S-1-5-19" || sid == operator_sid;
        if (safe && readonly_config && sid != "S-1-5-18" && sid != "S-1-5-32-544") {
            constexpr DWORD writes = FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES |
                FILE_DELETE_CHILD | DELETE | WRITE_DAC | WRITE_OWNER | GENERIC_WRITE | GENERIC_ALL;
            safe = (ace->Mask & writes) == 0;
        }
    }
    LocalFree(descriptor);
    if (!safe) throw std::runtime_error("Service data ACL grants unexpected identities; rerun the reviewed installer");
}

void require_admin_owned_directory(const QString& directory) {
    PSID owner = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const auto code = GetNamedSecurityInfoW(const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(directory.utf16())),
        SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr, nullptr, &descriptor);
    LPWSTR text = nullptr;
    const bool obtained = code == ERROR_SUCCESS && owner && ConvertSidToStringSidW(owner, &text);
    const auto sid = obtained ? QString::fromWCharArray(text) : QString{};
    if (text) LocalFree(text);
    if (descriptor) LocalFree(descriptor);
    if (sid != "S-1-5-18" && sid != "S-1-5-32-544")
        throw std::runtime_error("Service directories must be owned by SYSTEM or Administrators");
}

QString engine_pipe_name(const QString& data_directory) {
    const auto canonical = QDir(data_directory).canonicalPath();
    const auto path = (canonical.isEmpty() ? QDir(data_directory).absolutePath() : canonical).toLower();
    return "OpenNVR.Control." + QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex().left(20));
}

QJsonObject engine_request(const QString& directory, const QJsonObject& request, int timeout_ms, const QString& suffix) {
    QLocalSocket socket;
    socket.connectToServer(engine_pipe_name(directory) + suffix);
    if (!socket.waitForConnected(timeout_ms)) return {{"ok", false}, {"error", "Engine belum terhubung"}};
    auto message = request;
    message["v"] = 1;
    const auto encoded = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    if (encoded.size() > 64 * 1024 || socket.write(encoded) != encoded.size() || !socket.waitForBytesWritten(timeout_ms))
        return {{"ok", false}, {"error", "Kontrol engine gagal dikirim"}};
    QByteArray reply;
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < timeout_ms && reply.size() <= 1024 * 1024) {
        reply += socket.readAll();
        if (reply.contains('\n')) break;
        if (!socket.waitForReadyRead(std::max(1, timeout_ms - static_cast<int>(timer.elapsed())))) {
            reply += socket.readAll(); break;
        }
    }
    const auto document = QJsonDocument::fromJson(reply.trimmed());
    if (!reply.contains('\n') || reply.size() > 1024 * 1024 || !document.isObject())
        return {{"ok", false}, {"error", "Respons engine tidak valid atau timeout"}};
    socket.write("\n", 1);
    socket.waitForBytesWritten(1000);
    return document.object();
}

class LocalPipeServer::Impl {
public:
    Impl(const QString& name, const QString& sid, std::function<QJsonObject(const QJsonObject&)> handler)
        : handler_(std::move(handler)) {
        if (!QRegularExpression("^[A-Za-z0-9._-]+$").match(name).hasMatch()) throw std::invalid_argument("Invalid pipe name");
        PSID validated = nullptr;
        if (!ConvertStringSidToSidW(reinterpret_cast<LPCWSTR>(sid.utf16()), &validated)) throw std::invalid_argument("Invalid operator SID");
        LocalFree(validated);
        const auto sddl = QString("D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;LS)(A;;GRGW;;;%1)").arg(sid);
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(reinterpret_cast<LPCWSTR>(sddl.utf16()), SDDL_REVISION_1, &descriptor, nullptr))
            throw std::runtime_error("Cannot configure local pipe security");
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
        const auto full_name = QString("\\\\.\\pipe\\") + name;
        pipe_ = CreateNamedPipeW(reinterpret_cast<LPCWSTR>(full_name.utf16()),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1, 65536, 65536, 2000, &security);
        LocalFree(descriptor);
        if (pipe_ == INVALID_HANDLE_VALUE) throw std::runtime_error("Engine pipe is already in use or unavailable");
        stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!stop_) { CloseHandle(pipe_); throw std::runtime_error("Cannot create engine stop event"); }
        worker_ = std::jthread([this] { serve(); });
    }

    ~Impl() {
        SetEvent(stop_);
        if (worker_.joinable()) worker_.join();
        CloseHandle(pipe_);
        CloseHandle(stop_);
    }

private:
    bool finish_io(OVERLAPPED& operation, DWORD& count, DWORD timeout = 3000) {
        HANDLE events[]{operation.hEvent, stop_};
        const auto wait = WaitForMultipleObjects(2, events, FALSE, timeout);
        if (wait != WAIT_OBJECT_0) {
            CancelIoEx(pipe_, &operation);
            GetOverlappedResult(pipe_, &operation, &count, TRUE);
            return false;
        }
        return GetOverlappedResult(pipe_, &operation, &count, FALSE);
    }
    bool transfer(bool write, void* data, DWORD size, DWORD& count) {
        OVERLAPPED operation{};
        operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!operation.hEvent) return false;
        const bool immediate = write ? WriteFile(pipe_, data, size, &count, &operation)
                                     : ReadFile(pipe_, data, size, &count, &operation);
        const bool success = immediate || (GetLastError() == ERROR_IO_PENDING && finish_io(operation, count));
        CloseHandle(operation.hEvent);
        return success;
    }
    void serve() noexcept {
        while (WaitForSingleObject(stop_, 0) != WAIT_OBJECT_0) {
            OVERLAPPED connection{};
            connection.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!connection.hEvent) return;
            const bool connected = ConnectNamedPipe(pipe_, &connection);
            const auto code = connected ? ERROR_SUCCESS : GetLastError();
            DWORD count = 0;
            const bool ready = connected || code == ERROR_PIPE_CONNECTED ||
                (code == ERROR_IO_PENDING && finish_io(connection, count, INFINITE));
            CloseHandle(connection.hEvent);
            if (!ready) { DisconnectNamedPipe(pipe_); continue; }
            QByteArray request;
            std::array<char, 4096> buffer{};
            while (request.size() <= 64 * 1024 && !request.contains('\n') &&
                   transfer(false, buffer.data(), static_cast<DWORD>(buffer.size()), count) && count > 0) {
                request.append(buffer.data(), count);
            }
            QJsonObject response{{"v", 1}, {"ok", false}, {"error", "Invalid local request"}};
            const auto document = QJsonDocument::fromJson(request.trimmed());
            if (request.size() <= 64 * 1024 && request.contains('\n') && document.isObject() && document.object()["v"].toInt() == 1) {
                try { response = handler_(document.object()); response["v"] = 1; }
                catch (const std::exception&) { response = {{"v",1},{"ok",false},{"error","Engine request failed"}}; }
            }
            auto reply = QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n';
            if (reply.size() > 1024 * 1024) reply = "{\"v\":1,\"ok\":false,\"error\":\"Response too large\"}\n";
            DWORD sent = 0;
            while (sent < static_cast<DWORD>(reply.size())) {
                if (!transfer(true, reply.data() + sent, static_cast<DWORD>(reply.size()) - sent, count) || count == 0) break;
                sent += count;
            }
            // Client ACK confirms delivery without an unbounded FlushFileBuffers.
            char acknowledgement = 0;
            if (sent == static_cast<DWORD>(reply.size())) transfer(false, &acknowledgement, 1, count);
            DisconnectNamedPipe(pipe_);
        }
    }
    std::function<QJsonObject(const QJsonObject&)> handler_;
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    HANDLE stop_ = nullptr;
    std::jthread worker_;
};

LocalPipeServer::LocalPipeServer(const QString& name, const QString& sid,
    std::function<QJsonObject(const QJsonObject&)> handler)
    : impl_(std::make_unique<Impl>(name, sid, std::move(handler))) {}
LocalPipeServer::~LocalPipeServer() = default;

} // namespace opennvr
