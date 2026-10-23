#pragma once

#include <QObject>
#include <QJsonObject>
#include <QLocalSocket>
#include <QQueue>
#include <QTimer>

class EngineClient : public QObject {
    Q_OBJECT
public:
    explicit EngineClient(QString directory, QObject* parent = nullptr);
    void request(QJsonObject request);
signals:
    void replied(QString command, QJsonObject response);
private:
    void next();
    void finish();
    QString directory_;
    QLocalSocket socket_;
    QTimer timeout_;
    QQueue<QJsonObject> queue_;
    QByteArray bytes_;
    QJsonObject active_;
    QJsonObject response_;
    bool received_ = false;
    bool active_request_ = false;
};
