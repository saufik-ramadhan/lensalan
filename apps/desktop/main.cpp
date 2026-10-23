#include "backend.hpp"
#include "media/probe.hpp"
#include "platform/credentials.hpp"
#include "detection/person_detector.hpp"

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

namespace {
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
        if (QString::fromUtf8(argv[i]) == "--ai-self-test") return ai_self_test(argc, argv);
        if (QString::fromUtf8(argv[i]) == "--camera-probe") return camera_probe(argc, argv);
    }
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName("OpenNVR");
    QCoreApplication::setApplicationName("OpenNVR");
    QQuickStyle::setStyle("Basic");
    Backend backend(QDir::currentPath());
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("OpenNVR", "Main");
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
