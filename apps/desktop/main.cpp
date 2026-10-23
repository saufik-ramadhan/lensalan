#include "backend.hpp"
#include "live_video.hpp"
#include "media/probe.hpp"
#include "platform/credentials.hpp"
#include "detection/person_detector.hpp"
#include "detection/scheduler.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSaveFile>
#include <QTimer>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QFile>
#include <QTextStream>

namespace {
int profile_test(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir directory(QDir::currentPath() + "/build/profile-test-XXXXXX");
    if (!directory.isValid()) return 2;
    qputenv("OPENNVR_DATA_DIR", (directory.path() + "/engine").toUtf8());
    try {
        Backend backend(directory.path());
        const auto secret = QStringLiteral("local-test-password-only");
        for (int n = 1; n <= 12; ++n) backend.saveProfile(QString("camera-%1").arg(n), QString("Test %1").arg(n), QString("192.0.2.%1").arg(n), "test-account", secret);
        if (backend.profiles().size() != 12) throw std::runtime_error("Twelve profiles were not saved");
        backend.saveProfile("camera-13", "Rejected", "192.0.2.13", "test-account", secret);
        if (backend.profiles().size() != 12) throw std::runtime_error("Thirteenth camera accepted");
        backend.saveProfile("camera-1", "Edited", "192.0.2.1", "test-account", {});
        const auto credential_file = directory.path() + "/.local/cameras/camera-1.json";
        if (opennvr::load_camera_credential(credential_file).password != secret) throw std::runtime_error("Editing lost stored password");
        QFile credential(credential_file);
        if (!credential.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot read encrypted test credential");
        if (credential.readAll().contains(secret.toUtf8())) throw std::runtime_error("Credential saved as plaintext");
        backend.removeProfile("camera-1");
        if (backend.profiles().size() != 11 || !QFileInfo::exists(credential_file)) throw std::runtime_error("Profile removal damaged stored credential");
        Backend reloaded(directory.path());
        if (reloaded.profiles().size() != 11) throw std::runtime_error("Saved catalog did not reload");
        QSaveFile report(".local/profile-validation.json");
        if (!report.open(QIODevice::WriteOnly)) return 2;
        report.write("{\"success\":true,\"note\":\"12-profile DPAPI, edit, removal, reload, camera limit\"}\n"); report.commit();
        return 0;
    } catch (const std::exception& error) { QTextStream(stderr) << error.what() << Qt::endl; return 1; }
}
void collect_live_items(QQuickItem* parent, QList<LiveVideo*>& output) {
    if (auto* live = qobject_cast<LiveVideo*>(parent)) output.append(live);
    for (auto* child : parent->childItems()) collect_live_items(child, output);
}
int ai_scheduler_test(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QJsonObject output;
    bool success = false;
    try {
        const auto image = QImage(".deps/models/basketball1.png").convertToFormat(QImage::Format_BGR888);
        if (image.isNull()) throw std::runtime_error("AI test image missing");
        opennvr::InferenceScheduler scheduler(std::filesystem::path(QDir::currentPath().toStdWString()));
        std::vector<std::shared_ptr<opennvr::AnalysisChannel>> channels;
        for (int n = 0; n < 12; ++n) {
            channels.push_back(scheduler.channel());
            scheduler.submit(channels.back(), image.constBits(), image.width(), image.height(), image.bytesPerLine());
        }
        QJsonArray results;
        const auto began = std::chrono::steady_clock::now();
        std::vector<bool> completed(12);
        while (std::chrono::steady_clock::now() - began < std::chrono::seconds(30) && results.size() < 12) {
            for (int n = 0; n < 12; ++n) if (!completed[n]) if (auto result = scheduler.take(channels[n])) {
                if (result->people.empty()) throw std::runtime_error("Positive person image not detected");
                completed[n] = true;
                results.append(QJsonObject{{"camera",n+1},{"people",static_cast<int>(result->people.size())},{"inferenceMs",result->inference_ms}});
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        success = results.size() == 12 && scheduler.sessions() == 1;
        output = {{"success",success},{"modelSessions",scheduler.sessions()},{"results",results},
            {"note","Twelve channels on one positive test image; not a 12-camera RTSP benchmark"}};
    } catch (const std::exception& error) { output["error"] = QString::fromUtf8(error.what()); }
    QSaveFile file(".local/ai-scheduler-validation.json");
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(output).toJson()) < 0 || !file.commit()) return 2;
    return success ? 0 : 1;
}
int ai_self_test(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QJsonObject output;
    int exit_code = 1;
    try {
        opennvr::PersonDetector detector(std::filesystem::path(QDir::currentPath().toStdWString()));
        const auto image = QImage(".deps/models/basketball1.png").convertToFormat(QImage::Format_BGR888);
        if (image.isNull()) throw std::runtime_error("AI test image is missing in .deps/models/basketball1.png");
        const auto result = detector.infer(image.constBits(), image.width(), image.height(), image.bytesPerLine());
        QJsonArray boxes;
        for (const auto& person : result.people) boxes.append(QJsonObject{{"x", person.x}, {"y", person.y},
            {"width", person.width}, {"height", person.height}, {"confidence", person.confidence}});
        output = {{"provider", QString::fromStdString(detector.provider())},
                  {"people", boxes}, {"inferenceMs", result.inference_ms}, {"success", !result.people.empty()}};
        exit_code = result.people.empty() ? 1 : 0;
    } catch (const std::exception& error) { output["error"] = QString::fromUtf8(error.what()); }
    QDir().mkpath(".local");
    QSaveFile report(".local/ai-selftest.json");
    if (!report.open(QIODevice::WriteOnly) || report.write(QJsonDocument(output).toJson()) < 0 || !report.commit()) return 2;
    return exit_code;
}

int camera_probe(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    auto option = [&args](const QString& name, const QString& fallback) {
        const auto index = args.indexOf(name);
        return index >= 0 && index + 1 < args.size() ? args[index + 1] : fallback;
    };
    const auto destination = option("--report", ".local/camera-probe.json");
    QJsonObject output;
    int exit_code = 1;
    try {
        const auto credential = opennvr::load_camera_credential(option("--config", ".local/camera.json"));
        opennvr::ProbeConfig config;
        config.synthetic = false;
        config.host = credential.host.toStdString();
        config.username = credential.username.toStdString();
        config.password = credential.password.toStdString();
        config.substream = args.contains("--substream");
        config.test_recording = args.contains("--record");
        config.detect_people = args.contains("--detect");
        config.project_root = std::filesystem::path(QDir::currentPath().toStdWString());
        config.duration = std::chrono::seconds(option("--seconds", "12").toInt());
        const auto report = opennvr::run_probe(config);
        bool playback = false;
        if (!report.recording.empty() && report.success) playback = opennvr::validate_recording(report.recording, true);
        output = {{"success", report.success}, {"width", report.width}, {"height", report.height},
            {"packets", static_cast<qint64>(report.packets)}, {"keyframes", static_cast<qint64>(report.keyframes)},
            {"bytes", static_cast<qint64>(report.bytes)}, {"elapsedSeconds", report.elapsed_seconds},
            {"maxGopSeconds", report.maximum_gop_seconds}, {"error", QString::fromStdString(report.error)},
            {"analyzedFrames", static_cast<qint64>(report.analyzed_frames)},
            {"personFrames", static_cast<qint64>(report.person_frames)},
            {"confirmedFrames", static_cast<qint64>(report.confirmed_frames)},
            {"events", static_cast<qint64>(report.events)}, {"averageInferenceMs", report.average_inference_ms},
            {"stoppedDuringEvent", report.stopped_during_event},
            {"caps", QString::fromStdString(report.caps)}, {"hardwareDecodedRecording", playback},
            {"recording", QString::fromStdWString(report.recording.wstring())}};
        exit_code = report.success && (!config.test_recording || playback) ? 0 : 1;
    } catch (const std::exception& error) { output["error"] = QString::fromUtf8(error.what()); }
    QDir().mkpath(QFileInfo(destination).absolutePath());
    QSaveFile report(destination);
    if (!report.open(QIODevice::WriteOnly) || report.write(QJsonDocument(output).toJson()) < 0 || !report.commit()) return 2;
    return exit_code;
}
} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (QString::fromUtf8(argv[i]) == "--profile-test") return profile_test(argc, argv);
        if (QString::fromUtf8(argv[i]) == "--ai-scheduler-test") return ai_scheduler_test(argc, argv);
        if (QString::fromUtf8(argv[i]) == "--ai-self-test") return ai_self_test(argc, argv);
        if (QString::fromUtf8(argv[i]) == "--camera-probe") return camera_probe(argc, argv);
    }
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName("OpenNVR");
    QCoreApplication::setApplicationName("OpenNVR");
    QQuickStyle::setStyle("Basic");
    qmlRegisterType<LiveVideo>("OpenNVR.Live", 1, 0, "LiveVideo");
    Backend backend(QDir::currentPath());
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("OpenNVR", "Main");
    if (app.arguments().contains("--ui-grid-test")) {
        const auto i = app.arguments().indexOf("--grid-test-count");
        const auto count = i >= 0 ? app.arguments().value(i + 1).toInt() : 4;
        const QList<int> layouts{1,4,9,12};
        if (!layouts.contains(count) || engine.rootObjects().isEmpty()) return 2;
        auto* root = engine.rootObjects().first();
        root->findChild<QObject*>("grid-mode")->setProperty("currentIndex", layouts.indexOf(count));
        auto* timer = new QTimer(&app); timer->setInterval(100);
        const auto began = std::chrono::steady_clock::now();
        QObject::connect(timer, &QTimer::timeout, &app, [&, root, began, count] {
            QJsonArray videos;
            bool ready = backend.engineConnected() && !backend.cameras().isEmpty();
            int active = 0;
            QList<LiveVideo*> live_items;
            if (auto* window = qobject_cast<QQuickWindow*>(root)) collect_live_items(window->contentItem(), live_items);
            for (auto* item : live_items) {
                if (!item->active() || item->cameraId().isEmpty()) continue;
                ++active;
                ready &= item->frameCount() >= 5 && item->status() == "Live";
                videos.append(QJsonObject{{"id",item->cameraId()},{"frames",item->frameCount()},{"status",item->status()}});
            }
            ready &= active == std::min(count, static_cast<int>(backend.cameras().size()));
            const bool expired = std::chrono::steady_clock::now() - began > std::chrono::seconds(35);
            if (!ready && !expired) return;
            if (ready && !app.arguments().contains("--grid-no-snapshot")) if (auto* window = qobject_cast<QQuickWindow*>(root)) window->grabWindow().save(QString(".local/live-grid-%1.png").arg(count));
            QSaveFile report(QString(".local/ui-grid-%1.json").arg(count));
            if (report.open(QIODevice::WriteOnly)) {
                report.write(QJsonDocument(QJsonObject{{"success",ready},{"gridCount",count},{"videos",videos}}).toJson()); report.commit();
            }
            app.exit(ready ? 0 : 2);
        });
        timer->start();
    }
    if (app.arguments().contains("--ui-playback-test")) {
        auto* timer = new QTimer(&app);
        timer->setInterval(100);
        auto stage = std::make_shared<int>(0);
        const auto began = std::chrono::steady_clock::now();
        QObject::connect(timer, &QTimer::timeout, &app, [&, stage, began] {
            if (*stage == 0 && backend.engineConnected() && !backend.events().isEmpty()) {
                for (const auto& value : backend.events()) {
                    const auto event = value.toMap();
                    if (event["status"].toString() == "complete") {
                        backend.playEvent(event["id"].toString());
                        *stage = 1;
                        break;
                    }
                }
            } else if (*stage == 1 && backend.busy()) {
                *stage = 2;
            } else if (*stage == 2 && !backend.busy()) {
                const bool success = backend.status() == "Playback selesai";
                QSaveFile report(".local/ui-playback-validation.json");
                if (report.open(QIODevice::WriteOnly)) {
                    report.write(QJsonDocument(QJsonObject{{"success",success},{"status",backend.status()}}).toJson());
                    report.commit();
                }
                app.exit(success ? 0 : 2);
            }
            if (std::chrono::steady_clock::now() - began > std::chrono::seconds(25)) { backend.stop(); app.exit(2); }
        });
        timer->start();
    }
    if (app.arguments().contains("--smoke-test")) {
        QTimer::singleShot(1000, &app, [&] {
            if (engine.rootObjects().isEmpty()) { app.exit(2); return; }
            auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
            QDir().mkpath(".local");
            const bool saved = window && window->grabWindow().save(".local/workbench.png");
            app.exit(saved ? 0 : 2);
        });
    }
    return app.exec();
}
