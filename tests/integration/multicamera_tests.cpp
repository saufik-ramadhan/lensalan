#include "engine/live_hub.hpp"
#include "platform/camera_catalog.hpp"
#include "detection/scheduler.hpp"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <atomic>
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace opennvr;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class Action> void rejected(Action action) { bool failed = false; try { action(); } catch (...) { failed = true; } check(failed, "Invalid input was accepted"); }
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        QJsonArray list;
        for (int i = 1; i <= 12; ++i) list.append(QJsonObject{{"id",QString("camera-%1").arg(i)},{"name",QString("Kamera %1").arg(i)},{"credential",QString("cameras/camera-%1.json").arg(i)}});
        check(camera_sources({{"cameras",list}}, "C:/test", {}).size() == 12, "Twelve cameras rejected");
        auto too_many = list; too_many.append(list.first());
        rejected([&] { camera_sources({{"cameras",too_many}}, "C:/test", {}); });
        auto duplicate = list; duplicate[1] = list.first();
        rejected([&] { camera_sources({{"cameras",duplicate}}, "C:/test", {}); });
        rejected([&] { camera_sources({{"cameras",QJsonArray{QJsonObject{{"id","camera-1"},{"name","Test"},{"credential","../outside.json"}}}}}, "C:/test", {}); });
        check(camera_sources({}, "C:/test", "legacy.json").front().credential == "legacy.json", "Legacy credential migration changed");

        LiveHub hub;
        auto publish = [&](bool key, int value) {
            EncodedPacket packet; packet.keyframe = key; packet.caps = "video/x-h264,stream-format=byte-stream,alignment=au";
            packet.data = std::make_shared<std::vector<std::byte>>(15000, static_cast<std::byte>(value));
            hub.publish(packet);
        };
        publish(false, 1);
        check(hub.read({})["frames"].toArray().empty(), "Orphan delta exposed");
        publish(true, 2); publish(false, 3);
        const auto first = hub.read({});
        check(first["reset"].toBool() && first["frames"].toArray().first().toObject()["key"].toBool(), "Live did not start at keyframe");
        check(QByteArray::fromBase64(first["frames"].toArray().first().toObject()["data"].toString().toLatin1()).size() == 15000, "Media encoding corrupt");
        auto cursor = QJsonObject{{"cursor",first["cursor"]},{"generation",first["generation"]},{"epoch",first["epoch"]}};
        check(hub.read(cursor)["frames"].toArray().empty(), "Live repeated consumed packets");
        for (int n = 0; n < 500; ++n) publish(n % 20 == 0, n % 255);
        const auto recovered = hub.read(cursor);
        check(recovered["reset"].toBool() && recovered["frames"].toArray().first().toObject()["key"].toBool(), "Slow client did not recover at a keyframe");
        check(QJsonDocument(recovered).toJson().size() < 400000, "Live reply exceeded bounded batch");
        hub.reset(); check(hub.read({})["generation"] != recovered["generation"], "Reconnect generation unchanged");

        std::atomic_int active{0}, calls{0};
        InferenceScheduler scheduler({}, [&](const std::uint8_t* data, int, int, int) {
            check(++active == 1, "Multiple inference calls overlapped");
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
            DetectionResult result; result.inference_ms = data[0]; ++calls; --active; return result;
        });
        std::vector<std::shared_ptr<AnalysisChannel>> channels;
        for (int i = 0; i < 12; ++i) channels.push_back(scheduler.channel());
        rejected([&] { scheduler.channel(); });
        for (int repeat = 0; repeat < 30; ++repeat) for (int i = 0; i < 12; ++i) {
            const std::uint8_t data[]{static_cast<std::uint8_t>(i + 1),0,0};
            scheduler.submit(channels[i], data, 1, 1, 3);
        }
        std::vector<bool> completed(12);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline && std::count(completed.begin(), completed.end(), true) < 12) {
            for (int i = 0; i < 12; ++i) if (auto result = scheduler.take(channels[i])) {
                check(result->inference_ms == i + 1, "Inference result crossed camera channels"); completed[i] = true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        check(std::count(completed.begin(), completed.end(), true) == 12, "Inference scheduler starved a channel");
        check(calls < 360, "Stale frames were not replaced");
        InferenceScheduler failed({}, [](const std::uint8_t*, int, int, int) -> DetectionResult { throw std::runtime_error("test failure"); });
        auto channel = failed.channel(); const std::uint8_t pixel[]{0,0,0}; failed.submit(channel, pixel, 1, 1, 3);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        rejected([&] { failed.take(channel); });
        std::cout << "PASS camera limit/IDs/paths, legacy migration, media bounds/keyframe recovery, scheduler fairness/latest-frame/error isolation\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
