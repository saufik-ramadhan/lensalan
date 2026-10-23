#include "domain/event.hpp"
#include "media/encoded_ring.hpp"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace std::chrono_literals;
using namespace opennvr;

void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

EncodedPacket packet(int seconds, bool keyframe, std::size_t bytes = 10,
                     std::uint64_t generation = 0) {
    return {std::chrono::seconds(seconds), static_cast<std::uint64_t>(seconds),
        static_cast<std::uint64_t>(seconds), 1, generation, keyframe,
        std::make_shared<const std::vector<std::byte>>(bytes), "video/x-h264"};
}

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"confirmation does not refresh on old positives", [] {
            ConfirmationGate gate;
            check(!gate.sample(0s, true), "single frame must not trigger");
            check(gate.sample(300ms, true), "two positive frames must trigger");
            check(!gate.sample(600ms, false), "negative frame must not refresh event");
        }},
        {"stale detections expire", [] {
            ConfirmationGate gate;
            gate.sample(0s, true);
            check(!gate.sample(3s, true), "stale positive counted");
            check(gate.sample(3300ms, true), "fresh positive not counted");
            gate.reset();
            check(!gate.sample(0s, true), "reset retained history");
        }},
        {"person extends post-roll including deadline boundary", [] {
            EventWindow window(20s);
            check(window.person_seen(5s), "event did not start");
            check(!window.expire(24s), "event closed too soon");
            check(!window.person_seen(25s), "boundary split the event");
            check(!window.expire(44s), "post-roll was not extended");
            check(window.expire(45s), "timer did not close event");
            check(!window.expire(46s), "event closed twice");
        }},
        {"ring starts on keyframe and keeps preceding GOP", [] {
            EncodedRing ring(10s, 4096);
            ring.push(packet(0, false));
            check(ring.size() == 0, "accepted orphan delta");
            for (int i = 1; i <= 17; ++i) ring.push(packet(i, i % 4 == 1));
            const auto snapshot = ring.snapshot_from(7s);
            check(snapshot.front().received == 5s, "pre-roll not anchored at preceding keyframe");
            check(snapshot.back().received == 17s, "latest packets missing");
        }},
        {"memory pressure drops complete GOP and waits if needed", [] {
            EncodedRing ring(10s, 25);
            ring.push(packet(0, true));
            ring.push(packet(1, false));
            ring.push(packet(2, false));
            check(ring.size() == 0 && ring.bytes() == 0, "retained undecodable GOP");
            ring.push(packet(3, false));
            check(ring.size() == 0, "accepted delta after pressure");
            ring.push(packet(4, true));
            check(ring.bytes() == 10, "did not recover at keyframe");
        }},
        {"generation changes cannot mix streams", [] {
            EncodedRing ring(10s, 4096);
            ring.push(packet(0, true));
            ring.push(packet(1, false, 10, 1));
            check(ring.size() == 0, "generation mixed");
            ring.push(packet(2, true, 10, 1));
            check(ring.size() == 1, "new epoch did not recover");
        }},
        {"oversized packet and codec changes invalidate history", [] {
            EncodedRing ring(10s, 25);
            ring.push(packet(0, true));
            ring.push(packet(1, true, 26));
            check(ring.bytes() == 0, "oversized packet broke hard bound");
            ring.push(packet(2, true));
            auto changed = packet(3, false);
            changed.caps = "video/x-h265";
            ring.push(changed);
            check(ring.size() == 0, "codec config mixed");
        }},
        {"invalid settings and clock reset rejected", [] {
            bool rejected = false;
            try { ConfirmationGate invalid(4, 3); } catch (const std::invalid_argument&) { rejected = true; }
            check(rejected, "invalid settings accepted");
            ConfirmationGate gate;
            gate.sample(1s, true);
            rejected = false;
            try { gate.sample(0s, true); } catch (const std::invalid_argument&) { rejected = true; }
            check(rejected, "clock reset was silent");
        }}
    };
    int failed = 0;
    for (const auto& [name, test] : tests) {
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) {
            ++failed;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    }
    return failed == 0 ? 0 : 1;
}
