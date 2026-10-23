#include "storage/event_store.hpp"

#include <sqlite3.h>
#include <algorithm>
#include <mutex>
#include <stdexcept>

namespace opennvr {
namespace {
std::string utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}
class Statement {
public:
    Statement(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &value_, nullptr) != SQLITE_OK)
            throw std::runtime_error("SQLite statement preparation failed");
    }
    ~Statement() { sqlite3_finalize(value_); }
    void text(int index, const std::string& value) {
        if (sqlite3_bind_text(value_, index, value.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
            throw std::runtime_error("SQLite parameter binding failed");
    }
    void number(int index, std::int64_t value) {
        if (sqlite3_bind_int64(value_, index, value) != SQLITE_OK)
            throw std::runtime_error("SQLite parameter binding failed");
    }
    bool row() {
        const auto code = sqlite3_step(value_);
        if (code != SQLITE_ROW && code != SQLITE_DONE) throw std::runtime_error("SQLite write/read failed");
        return code == SQLITE_ROW;
    }
    std::string text(int index) const {
        const auto* value = sqlite3_column_text(value_, index);
        return value ? reinterpret_cast<const char*>(value) : "";
    }
    std::int64_t number(int index) const { return sqlite3_column_int64(value_, index); }
private:
    sqlite3_stmt* value_ = nullptr;
};
void execute(sqlite3* db, const char* sql) {
    if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK)
        throw std::runtime_error("SQLite schema/transaction failed");
}
} // namespace

class EventStore::Impl {
public:
    Impl(const std::filesystem::path& database, const std::filesystem::path& recordings) {
        std::filesystem::create_directories(recordings);
        root_ = std::filesystem::canonical(recordings);
        if (!database.parent_path().empty()) std::filesystem::create_directories(database.parent_path());
        const auto filename = utf8(database);
        if (sqlite3_open_v2(filename.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
            if (db_) sqlite3_close(db_);
            db_ = nullptr;
            throw std::runtime_error("Cannot open the event database");
        }
        try {
            sqlite3_busy_timeout(db_, 5000);
            execute(db_, "PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; PRAGMA foreign_keys=ON;");
            {
                Statement version(db_, "PRAGMA user_version");
                version.row();
                if (version.number(0) > 1) throw std::runtime_error("Database was created by a newer OpenNVR version");
            }
            execute(db_, "BEGIN IMMEDIATE; "
                "CREATE TABLE IF NOT EXISTS events(id TEXT PRIMARY KEY,camera_id TEXT NOT NULL,"
                "start_ms INTEGER NOT NULL,end_ms INTEGER NOT NULL DEFAULT 0,status TEXT NOT NULL,reason TEXT NOT NULL DEFAULT '');"
                "CREATE TABLE IF NOT EXISTS segments(event_id TEXT NOT NULL REFERENCES events(id) ON DELETE CASCADE,"
                "number INTEGER NOT NULL,path TEXT NOT NULL UNIQUE,start_ms INTEGER NOT NULL,end_ms INTEGER NOT NULL DEFAULT 0,"
                "status TEXT NOT NULL,bytes INTEGER NOT NULL DEFAULT 0,PRIMARY KEY(event_id,number));"
                "CREATE INDEX IF NOT EXISTS events_time ON events(start_ms DESC);"
                "CREATE TABLE IF NOT EXISTS incidents(id INTEGER PRIMARY KEY,time_ms INTEGER NOT NULL,code TEXT NOT NULL,message TEXT NOT NULL);"
                "PRAGMA user_version=1; COMMIT;");
        } catch (...) { sqlite3_close(db_); db_ = nullptr; throw; }
    }
    ~Impl() { if (db_) sqlite3_close(db_); }

    std::filesystem::path checked_path(const std::string& relative) const {
        const auto rel = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(relative.data()), relative.size()));
        if (rel.empty() || rel.is_absolute() || rel.has_root_name()) throw std::invalid_argument("Recording path must be relative");
        for (const auto& part : rel) if (part == "..") throw std::invalid_argument("Recording path traversal rejected");
        const auto target = std::filesystem::weakly_canonical(root_ / rel);
        const auto match = std::mismatch(root_.begin(), root_.end(), target.begin(), target.end());
        if (match.first != root_.end() || target == root_) throw std::invalid_argument("Recording path escapes managed storage");
        return target;
    }

    std::vector<StoredEvent> query(std::int64_t from, std::int64_t until, const std::string& camera, int limit, const std::string& id = {}) {
        Statement statement(db_, "SELECT id,camera_id,start_ms,end_ms,status,reason FROM events "
            "WHERE (?1='' OR id=?1) AND start_ms<?3 AND (end_ms=0 OR end_ms>=?2) "
            "AND (?4='' OR camera_id=?4) ORDER BY start_ms DESC LIMIT ?5");
        statement.text(1, id); statement.number(2, from); statement.number(3, until);
        statement.text(4, camera); statement.number(5, std::clamp(limit, 1, 100));
        std::vector<StoredEvent> result;
        while (statement.row()) {
            StoredEvent event{statement.text(0), statement.text(1), statement.text(4), statement.text(5),
                statement.number(2), statement.number(3), {}};
            Statement segments(db_, "SELECT number,path,status,start_ms,end_ms,bytes FROM segments WHERE event_id=?1 ORDER BY number");
            segments.text(1, event.id);
            while (segments.row()) event.segments.push_back({static_cast<int>(segments.number(0)), segments.text(1),
                segments.text(2), segments.number(3), segments.number(4), static_cast<std::uint64_t>(segments.number(5))});
            result.push_back(std::move(event));
        }
        return result;
    }

    sqlite3* db_ = nullptr;
    std::filesystem::path root_;
    std::mutex mutex_;
};

EventStore::EventStore(const std::filesystem::path& database, const std::filesystem::path& recordings)
    : impl_(std::make_unique<Impl>(database, recordings)) {}
EventStore::~EventStore() = default;
std::filesystem::path EventStore::checked_path(const std::string& relative) const { return impl_->checked_path(relative); }

void EventStore::update(const RecordingUpdate& value) {
    std::lock_guard lock(impl_->mutex_);
    auto* db = impl_->db_;
    if (value.stage == RecordingStage::event_started) {
        Statement insert(db, "INSERT INTO events(id,camera_id,start_ms,status) VALUES(?1,?2,?3,'recording')");
        insert.text(1, value.event_id); insert.text(2, value.camera_id); insert.number(3, value.start_utc_ms); insert.row();
    } else if (value.stage == RecordingStage::segment_started) {
        const auto relative = utf8(value.file.lexically_relative(impl_->root_));
        impl_->checked_path(relative);
        Statement insert(db, "INSERT INTO segments(event_id,number,path,start_ms,status) VALUES(?1,?2,?3,?4,'writing')");
        insert.text(1, value.event_id); insert.number(2, value.segment); insert.text(3, relative);
        insert.number(4, value.start_utc_ms); insert.row();
    } else if (value.stage == RecordingStage::segment_finished) {
        Statement lookup(db, "SELECT path FROM segments WHERE event_id=?1 AND number=?2");
        lookup.text(1, value.event_id); lookup.number(2, value.segment);
        if (!lookup.row()) throw std::runtime_error("Recording segment missing from index");
        std::error_code error;
        const auto size = std::filesystem::file_size(impl_->checked_path(lookup.text(0)), error);
        Statement finish(db, "UPDATE segments SET end_ms=?3,status=?4,bytes=?5 WHERE event_id=?1 AND number=?2");
        finish.text(1, value.event_id); finish.number(2, value.segment); finish.number(3, value.end_utc_ms);
        finish.text(4, error ? "partial" : value.status); finish.number(5, error ? 0 : static_cast<std::int64_t>(size)); finish.row();
    } else {
        Statement finish(db, "UPDATE events SET end_ms=?2,status=?3,reason=?4 WHERE id=?1");
        finish.text(1, value.event_id); finish.number(2, value.end_utc_ms);
        finish.text(3, value.status); finish.text(4, value.reason); finish.row();
    }
}

std::vector<StoredEvent> EventStore::events(std::int64_t from, std::int64_t until, const std::string& camera, int limit) {
    std::lock_guard lock(impl_->mutex_);
    return impl_->query(from, until, camera, limit);
}
StoredEvent EventStore::event(const std::string& id) {
    std::lock_guard lock(impl_->mutex_);
    auto result = impl_->query(0, INT64_MAX, {}, 1, id);
    if (result.empty()) throw std::runtime_error("Event not found");
    return result.front();
}

void EventStore::incident(const std::string& code, const std::string& message, std::int64_t time) {
    std::lock_guard lock(impl_->mutex_);
    Statement insert(impl_->db_, "INSERT INTO incidents(time_ms,code,message) VALUES(?1,?2,?3)");
    insert.number(1, time); insert.text(2, code); insert.text(3, message); insert.row();
    execute(impl_->db_, "DELETE FROM incidents WHERE id < (SELECT COALESCE(MAX(id),0)-2000 FROM incidents)");
}

void EventStore::reconcile() {
    std::lock_guard lock(impl_->mutex_);
    execute(impl_->db_, "UPDATE events SET status='interrupted',reason='engine_restart',"
        "end_ms=MAX(start_ms,COALESCE((SELECT MAX(end_ms) FROM segments WHERE event_id=events.id),start_ms)) WHERE status='recording';"
        "UPDATE segments SET status='partial' WHERE status='writing';");
    Statement segments(impl_->db_, "SELECT event_id,number,path,status FROM segments");
    while (segments.row()) {
        const auto path = impl_->checked_path(segments.text(2));
        if (!std::filesystem::is_regular_file(path)) {
            auto partial = path; partial += ".part";
            const bool has_partial = std::filesystem::is_regular_file(partial);
            Statement missing(impl_->db_, "UPDATE segments SET status=?3,bytes=?4 WHERE event_id=?1 AND number=?2");
            missing.text(1, segments.text(0)); missing.number(2, segments.number(1));
            missing.text(3, has_partial ? "partial" : "missing");
            missing.number(4, has_partial ? static_cast<std::int64_t>(std::filesystem::file_size(partial)) : 0); missing.row();
            Statement event(impl_->db_, "UPDATE events SET status='partial',reason='missing_file' WHERE id=?1 AND status!='deleting'");
            event.text(1, segments.text(0)); event.row();
        }
    }
}

StorageState EventStore::retain(std::int64_t now, const RetentionPolicy& policy) {
    if (policy.max_age_ms <= 0 || policy.quota_bytes == 0) throw std::invalid_argument("Invalid retention policy");
    std::lock_guard lock(impl_->mutex_);
    StorageState state;
    auto measure = [&] {
        state.used_bytes = 0;
        // Include partial and active files; never follow directory symlinks.
        for (const auto& entry : std::filesystem::recursive_directory_iterator(impl_->root_)) {
            if (entry.is_regular_file() && !entry.is_symlink()) {
                const auto relative = utf8(entry.path().lexically_relative(impl_->root_));
                const auto path = impl_->checked_path(relative);
                state.used_bytes += std::filesystem::file_size(path);
            }
        }
        state.free_bytes = std::filesystem::space(impl_->root_).available;
    };
    try {
        measure();
        struct Candidate { std::string id; std::int64_t end; std::string status; };
        std::vector<Candidate> candidates;
        {
            Statement query(impl_->db_, "SELECT id,end_ms,status FROM events WHERE status!='recording' ORDER BY start_ms");
            while (query.row()) candidates.push_back({query.text(0), query.number(1), query.text(2)});
        }
        for (const auto& candidate : candidates) {
            const bool expired = candidate.end < now - policy.max_age_ms;
            const bool pressure = state.used_bytes >= policy.quota_bytes || state.free_bytes <= policy.reserve_bytes;
            if (!expired && !pressure && candidate.status != "deleting") continue;
            const auto id = candidate.id;
            Statement mark(impl_->db_, "UPDATE events SET status='deleting' WHERE id=?1"); mark.text(1, id); mark.row();
            Statement files(impl_->db_, "SELECT path FROM segments WHERE event_id=?1"); files.text(1, id);
            bool all_deleted = true;
            while (files.row()) {
                std::error_code error;
                const auto path = impl_->checked_path(files.text(0));
                std::filesystem::remove(path, error);
                if (error) all_deleted = false;
                auto partial = path; partial += ".part";
                std::filesystem::remove(partial, error);
                if (error) all_deleted = false;
                if (all_deleted && path.parent_path() != impl_->root_) { std::filesystem::remove(path.parent_path(), error); }
            }
            if (all_deleted) {
                Statement remove(impl_->db_, "DELETE FROM events WHERE id=?1"); remove.text(1, id); remove.row();
                ++state.deleted_events;
            } else state.error = "Some expired recording files are locked or unavailable";
            measure();
        }
        state.writable = state.used_bytes < policy.quota_bytes && state.free_bytes > policy.reserve_bytes;
        if (!state.writable) state.error = "Storage quota or free-space reserve reached";
    } catch (const std::exception&) {
        state.writable = false;
        state.error = "Storage inspection or cleanup failed";
    }
    return state;
}

} // namespace opennvr
