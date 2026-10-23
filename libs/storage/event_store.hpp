#pragma once

#include "domain/recording.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace opennvr {

struct StoredSegment {
    int number = 0;
    std::string path;
    std::string status;
    std::int64_t start_utc_ms = 0;
    std::int64_t end_utc_ms = 0;
    std::uint64_t bytes = 0;
};

struct StoredEvent {
    std::string id;
    std::string camera_id;
    std::string status;
    std::string reason;
    std::int64_t start_utc_ms = 0;
    std::int64_t end_utc_ms = 0;
    std::vector<StoredSegment> segments;
};

struct RetentionPolicy {
    std::int64_t max_age_ms = 14LL * 24 * 60 * 60 * 1000;
    std::uint64_t quota_bytes = 100ULL * 1000 * 1000 * 1000;
    std::uint64_t reserve_bytes = 10ULL * 1000 * 1000 * 1000;
};

struct StorageState {
    std::uint64_t used_bytes = 0;
    std::uint64_t free_bytes = 0;
    std::size_t deleted_events = 0;
    bool writable = true;
    std::string error;
};

class EventStore {
public:
    EventStore(const std::filesystem::path& database, const std::filesystem::path& recordings);
    ~EventStore();
    void update(const RecordingUpdate& update);
    std::vector<StoredEvent> events(std::int64_t from_utc_ms = 0,
        std::int64_t until_utc_ms = INT64_MAX, const std::string& camera = {}, int limit = 100);
    StoredEvent event(const std::string& id);
    std::filesystem::path checked_path(const std::string& relative) const;
    void reconcile();
    StorageState retain(std::int64_t now_utc_ms, const RetentionPolicy& policy);
    void incident(const std::string& code, const std::string& message, std::int64_t now_utc_ms);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace opennvr
