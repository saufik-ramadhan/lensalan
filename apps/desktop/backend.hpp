#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <atomic>
#include <thread>

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString cameraHost READ cameraHost NOTIFY changed)
    Q_PROPERTY(QString cameraUsername READ cameraUsername NOTIFY changed)
    Q_PROPERTY(QString log READ log NOTIFY changed)
    Q_PROPERTY(QString lastRecording READ lastRecording NOTIFY changed)
    Q_PROPERTY(QStringList capabilities READ capabilities CONSTANT)

public:
    explicit Backend(QString root, QObject* parent = nullptr);
    ~Backend() override;
    bool busy() const { return busy_; }
    QString status() const { return status_; }
    QString cameraHost() const { return host_; }
    QString cameraUsername() const { return username_; }
    QString log() const { return log_.join('\n'); }
    QString lastRecording() const { return recording_; }
    QStringList capabilities() const { return capabilities_; }

    Q_INVOKABLE void saveCamera(QString host, QString username, QString password);
    Q_INVOKABLE void startProbe(bool synthetic, bool substream, bool recording, bool preview);
    Q_INVOKABLE void startDetection(bool preview);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void openRecording();

signals:
    void changed();

private:
    void append(QString message);
    void launch(bool synthetic, bool substream, bool recording, bool preview, bool detection);
    QString root_;
    QString host_;
    QString username_;
    QString status_{"Siap untuk validasi satu kamera"};
    QString recording_;
    QStringList log_;
    QStringList capabilities_;
    bool busy_ = false;
    std::atomic_bool cancelled_{false};
    std::jthread worker_;
};
