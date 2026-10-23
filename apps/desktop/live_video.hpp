#pragma once
#include <QQuickItem>
#include <QImage>
#include <atomic>
#include <mutex>
#include <thread>

class LiveVideo : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QString directory READ directory WRITE setDirectory NOTIFY sourceChanged)
    Q_PROPERTY(QString cameraId READ cameraId WRITE setCameraId NOTIFY sourceChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY sourceChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY statusChanged)
public:
    explicit LiveVideo(QQuickItem* parent = nullptr);
    ~LiveVideo() override;
    QString directory() const { return directory_; }
    QString cameraId() const { return camera_id_; }
    bool active() const { return active_; }
    QString status() const { return status_; }
    int frameCount() const { return frames_; }
    void setDirectory(QString value);
    void setCameraId(QString value);
    void setActive(bool value);
signals:
    void sourceChanged();
    void statusChanged();
protected:
    void componentComplete() override;
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;
private:
    void run();
    void deliver(QImage image, QString status);
    QString directory_, camera_id_, status_{"Menunggu video"};
    bool active_ = true;
    int frames_ = 0;
    std::mutex mutex_;
    QImage image_;
    std::atomic_bool stopping_{false};
    std::jthread worker_;
};
