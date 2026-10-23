#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

class EngineClient;

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
    Q_PROPERTY(bool engineConnected READ engineConnected NOTIFY changed)
    Q_PROPERTY(QString engineState READ engineState NOTIFY changed)
    Q_PROPERTY(QString engineSummary READ engineSummary NOTIFY changed)
    Q_PROPERTY(bool enginePaused READ enginePaused NOTIFY changed)
    Q_PROPERTY(bool engineService READ engineService NOTIFY changed)
    Q_PROPERTY(QString engineMessage READ engineMessage NOTIFY changed)
    Q_PROPERTY(QVariantList events READ events NOTIFY changed)

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
    bool engineConnected() const { return engine_connected_; }
    QString engineState() const { return engine_state_; }
    QString engineSummary() const { return engine_summary_; }
    bool enginePaused() const { return engine_paused_; }
    bool engineService() const { return engine_service_; }
    QString engineMessage() const { return engine_message_; }
    QVariantList events() const { return events_; }

    Q_INVOKABLE void saveCamera(QString host, QString username, QString password);
    Q_INVOKABLE void startProbe(bool synthetic, bool substream, bool recording, bool preview);
    Q_INVOKABLE void startDetection(bool preview);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void openRecording();
    Q_INVOKABLE void startEngine();
    Q_INVOKABLE void pauseEngine(bool pause);
    Q_INVOKABLE void stopEngine();
    Q_INVOKABLE void refreshEvents(QString date, QString camera);
    Q_INVOKABLE void playEvent(QString id);

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
    EngineClient* engine_ = nullptr;
    QString engine_data_;
    QString engine_state_{"Belum terhubung"};
    QString engine_summary_;
    QString last_event_;
    QString filter_date_;
    QString filter_camera_;
    QVariantList events_;
    bool engine_connected_ = false;
    bool engine_paused_ = false;
    bool engine_service_ = false;
    QString engine_message_;
};
