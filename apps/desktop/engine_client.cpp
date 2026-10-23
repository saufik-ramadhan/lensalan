#include "engine_client.hpp"
#include "platform/local_pipe.hpp"

#include <QJsonDocument>

EngineClient::EngineClient(QString directory, QObject* parent)
    : QObject(parent), directory_(std::move(directory)) {
    timeout_.setSingleShot(true);
    connect(&timeout_, &QTimer::timeout, this, [this] { finish(); });
    connect(&socket_, &QLocalSocket::connected, this, [this] {
        auto payload = active_;
        payload["v"] = 1;
        socket_.write(QJsonDocument(payload).toJson(QJsonDocument::Compact) + '\n');
    });
    connect(&socket_, &QLocalSocket::readyRead, this, [this] {
        bytes_ += socket_.readAll();
        if (bytes_.size() > 1024 * 1024) { finish(); return; }
        if (received_ || !bytes_.contains('\n')) return;
        const auto document = QJsonDocument::fromJson(bytes_.trimmed());
        if (!document.isObject()) { finish(); return; }
        response_ = document.object();
        received_ = true;
        socket_.write("\n", 1);
        socket_.flush();
    });
    connect(&socket_, &QLocalSocket::disconnected, this, [this] { finish(); });
    connect(&socket_, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
        if (active_request_) finish();
    });
}

void EngineClient::request(QJsonObject request) {
    if (queue_.size() >= 10) return;
    if (request["command"].toString() == "status") {
        if (active_request_ && active_["command"].toString() == "status") return;
        for (const auto& queued : queue_) if (queued["command"].toString() == "status") return;
    }
    queue_.enqueue(std::move(request));
    next();
}
void EngineClient::next() {
    if (active_request_ || queue_.isEmpty()) return;
    active_request_ = true;
    active_ = queue_.dequeue();
    received_ = false;
    response_ = {};
    bytes_.clear();
    timeout_.start(2000);
    socket_.connectToServer(opennvr::engine_pipe_name(directory_));
}
void EngineClient::finish() {
    if (!active_request_) return;
    const auto command = active_["command"].toString();
    auto response = received_ ? response_ : QJsonObject{{"ok",false},{"error","Engine belum terhubung atau timeout"}};
    active_request_ = false;
    timeout_.stop();
    socket_.abort();
    emit replied(command, response);
    QTimer::singleShot(0, this, [this] { next(); });
}
