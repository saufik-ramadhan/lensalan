#include "storage/event_store.hpp"

#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace opennvr;
namespace fs = std::filesystem;

void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void write_file(const fs::path& file, std::size_t bytes = 10) {
    fs::create_directories(file.parent_path());
    std::ofstream stream(file, std::ios::binary);
    stream << std::string(bytes, 'x');
}
void begin(EventStore& store, const std::string& id, const fs::path& root, std::int64_t time) {
    store.update({RecordingStage::event_started, id, "camera-1", {}, 0, time, 0, "recording", {}});
    store.update({RecordingStage::segment_started, id, "camera-1", root / id / "0.mkv", 0, time, 0, "writing", {}});
}
void finish(EventStore& store, const std::string& id, std::int64_t time) {
    store.update({RecordingStage::segment_finished, id, "camera-1", {}, 0, 0, time, "complete", {}});
    store.update({RecordingStage::event_finished, id, "camera-1", {}, 0, 0, time, "complete", "post_roll_elapsed"});
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const auto base = fs::absolute(argv[1]) / ("storage-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(base);
    const std::vector<std::pair<const char*, std::function<void(fs::path)>>> tests = {
        {"lifecycle survives reopen and filters are parameterized", [](const fs::path& folder) {
            const auto root = folder / "recordings";
            {
                EventStore store(folder / "events.sqlite3", root);
                begin(store, "id'quoted", root, 1000);
                write_file(root / "id'quoted/0.mkv", 21);
                finish(store, "id'quoted", 2000);
            }
            EventStore store(folder / "events.sqlite3", root);
            store.reconcile();
            const auto event = store.event("id'quoted");
            check(event.status == "complete" && event.segments[0].bytes == 21, "event not durable");
            check(store.events(1500, 2500, "camera-1").size() == 1, "overlap filter rejected valid event");
            check(store.events(0, 2500, "other").empty(), "camera filter ignored");
            check(store.events(2100, 2500).empty(), "date filter ignored");
        }},
        {"restart marks active and missing files explicitly", [](const fs::path& folder) {
            const auto root = folder / "recordings";
            EventStore store(folder / "events.sqlite3", root);
            begin(store, "active", root, 1000);
            write_file(root / "active/0.mkv.part");
            store.reconcile();
            check(store.event("active").status == "partial", "missing finalized file not visible");
            check(store.event("active").segments[0].status == "partial", "unfinalized file incorrectly treated as missing");
            begin(store, "finished", root, 1000);
            write_file(root / "finished/0.mkv"); finish(store, "finished", 1500);
            store.reconcile();
            check(store.event("finished").status == "complete", "finalized event damaged by recovery");
        }},
        {"age retention only removes old terminal events", [](const fs::path& folder) {
            const auto root = folder / "recordings";
            EventStore store(folder / "events.sqlite3", root);
            begin(store, "old", root, 1000); write_file(root / "old/0.mkv"); finish(store, "old", 1100);
            begin(store, "recent", root, 9000); write_file(root / "recent/0.mkv"); finish(store, "recent", 9500);
            const auto state = store.retain(10000, {2000, 10000, 0});
            check(state.deleted_events == 1 && state.writable, "retention did not remove oldest");
            check(!fs::exists(root / "old/0.mkv") && fs::exists(root / "recent/0.mkv"), "wrong file deleted");
        }},
        {"quota never deletes an active event", [](const fs::path& folder) {
            const auto root = folder / "recordings";
            EventStore store(folder / "events.sqlite3", root);
            begin(store, "active", root, 1000); write_file(root / "active/0.mkv", 100);
            begin(store, "old", root, 100); write_file(root / "old/0.mkv", 100); finish(store, "old", 200);
            const auto state = store.retain(2000, {100000, 50, 0});
            check(state.deleted_events == 1 && !state.writable, "active file incorrectly freed or pressure ignored");
            check(fs::exists(root / "active/0.mkv"), "active file deleted");
        }},
        {"path traversal and absolute paths are rejected", [](const fs::path& folder) {
            EventStore store(folder / "events.sqlite3", folder / "recordings");
            for (const std::string input : {"../outside", "nested/../../outside", "/outside", "C:/outside"}) {
                bool rejected = false;
                try { store.checked_path(input); } catch (const std::invalid_argument&) { rejected = true; }
                check(rejected, "unsafe path accepted");
            }
        }},
        {"unindexed files count against quota but are never deleted", [](const fs::path& folder) {
            const auto root = folder / "recordings";
            EventStore store(folder / "events.sqlite3", root);
            write_file(root / "orphan.mkv", 100);
            const auto state = store.retain(10000, {1000, 50, 0});
            check(!state.writable && fs::exists(root / "orphan.mkv"), "unindexed file was deleted or not counted");
        }}
    };
    int failed = 0;
    for (std::size_t i = 0; i < tests.size(); ++i) {
        try { tests[i].second(base / std::to_string(i)); std::cout << "PASS " << tests[i].first << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << tests[i].first << ": " << error.what() << '\n'; }
    }
    // Leave fixtures in the ignored build directory for inspection; no recursive deletion.
    return failed == 0 ? 0 : 1;
}
