#include "ObjectStore.h"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace iiFileProvider;
namespace fs = std::filesystem;
static_assert(__cplusplus >= 202302L);
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
template<class F> void rejects(F f) {
    bool rejected = false; try { f(); } catch (const std::exception &) { rejected = true; }
    require(rejected, "operation must reject invalid input");
}
struct Database {
    sqlite3 *db = nullptr;
    explicit Database(const fs::path &path) {
        const auto u = path.u8string();
        require(sqlite3_open(reinterpret_cast<const char *>(u.c_str()), &db) == SQLITE_OK, "fixture database");
    }
    ~Database() { sqlite3_close(db); }
    void exec(const char *sql) { require(sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK, "fixture SQL"); }
    std::int64_t scalar(const char *sql) {
        sqlite3_stmt *q = nullptr;
        require(sqlite3_prepare_v2(db, sql, -1, &q, nullptr) == SQLITE_OK, "fixture query");
        const auto status = sqlite3_step(q); const auto result = sqlite3_column_int64(q, 0); sqlite3_finalize(q);
        require(status == SQLITE_ROW, "fixture scalar"); return result;
    }
};
int main() try {
    const auto root = fs::path(OBJECT_TEST_DIRECTORY) / ("journal-storage-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    const auto source = root/"input";
    { std::ofstream out(source, std::ios::binary); out << "legacy payload"; }
    ObjectRecord legacy;
    std::string legacySession;
    {
        ObjectStore store(root/"package", "migration", true);
        auto s = store.beginSession({"local:test", "editor", "Editor"}, "device", "legacy");
        legacySession = s.key; legacy = store.importFile(source, "Files/legacy", s.key);
        store.endSession(s.key);
    }
    const auto database = root/"package/objects.sqlite3";
    {
        Database db(database);
        db.exec("DROP TRIGGER object_journal_append; DROP TABLE object_journal; DROP TABLE raw_revisions; DROP TABLE work_sessions; PRAGMA user_version=3;");
    }
    {
        ObjectStore reader(root/"package", "migration", ObjectStore::Access::ReadOnly);
        require(reader.lookup(legacy.key)->validationKey == legacy.validationKey && reader.validate(legacy.key, 1), "schema 3 remains readable without migration");
        rejects([&] { reader.journal(); }); rejects([&] { reader.workSession(legacySession); });
        Database db(database); require(db.scalar("PRAGMA user_version") == 3, "reader does not migrate");
    }
    rejects([&] { ObjectStore wrong(root/"package", "wrong"); });
    { Database db(database); require(db.scalar("PRAGMA user_version") == 3, "wrong container leaves schema unchanged"); }
    std::int64_t cursor = 0;
    {
        ObjectStore writer(root/"package", "migration");
        Database db(database); require(db.scalar("PRAGMA user_version") == 5, "schema 3 upgrades atomically");
        require(writer.validate(legacy.key, 1) && writer.lookup(legacy.key)->validationKey == legacy.validationKey, "migration retains legacy validation key");
        require(writer.workSession(legacySession).context == ObjectSessionContext{}, "backfilled context is explicitly unknown");
        auto journal = writer.journal();
        require(journal.size() == 1 && journal[0].objectKey == legacy.key, "legacy history backfilled exactly once");
        cursor = journal.back().sequence;
        auto s = writer.beginSession({"local:test", "editor", "Editor"}, "device", "new");
        auto other = writer.beginSession({"local:test", "other", "Other"}, "device", "other");
        ObjectSessionQuery page; page.limit = 1;
        const auto first = writer.sessions(page); page.afterSequence = first.back().sequence;
        const auto second = writer.sessions(page);
        require(first[0].session.key == legacySession && second[0].session.key == s.key, "session keyset pagination");
        page = {}; page.activeOnly = true; require(writer.sessions(page).size() == 2, "active-session filter");
        writer.move(legacy.key, "Files/renamed", 1, s.key);
        ObjectJournalQuery query; query.afterSequence = cursor; query.objectKey = legacy.key; query.sessionKey = s.key;
        auto tail = writer.journal(query); require(tail.size() == 1 && tail[0].operation == "move", "combined object/session/cursor filters");
        query.sessionKey = other.key; require(writer.journal(query).empty(), "session mismatch excludes rows");
        rejects([&] { writer.importFiles({{source, "Files/new", {}}, {source, "Files/renamed", {}}}, s.key); });
        require(writer.count() == 1 && writer.journal().size() == 2, "failed batch rolls back published journal rows");
        std::stop_source stop; stop.request_stop(); ObjectWriteOptions cancelled; cancelled.cancellation = stop.get_token();
        rejects([&] { writer.importFile(source, "Files/cancelled", s.key, cancelled); });
        require(writer.journal().size() == 2, "cancelled import leaves no journal row");
        ObjectStore reader(root/"package", "migration", ObjectStore::Access::ReadOnly);
        require(reader.journal().size() == 2 && reader.sessions().size() == 3, "WAL reader sees committed work while writer is open");
        cursor = writer.journal().back().sequence;
        writer.endSession(s.key); writer.endSession(other.key);
    }
    { Database db(database); db.exec("VACUUM;"); }
    {
        ObjectStore writer(root/"package", "migration");
        require(writer.journal().size() == 2, "reopening never duplicates backfill");
        auto s = writer.beginSession({"local:test", "editor", "Editor"}, "device", "after vacuum");
        writer.erase(legacy.key, 2, s.key);
        ObjectJournalQuery query; query.afterSequence = cursor;
        const auto tail = writer.journal(query);
        require(tail.size() == 1 && tail[0].sequence > cursor && tail[0].deleted, "persistent cursor survives vacuum and reopen");
        writer.extract(legacy.key, 1, root/"legacy-restored");
        require(fs::file_size(root/"legacy-restored") == fs::file_size(source), "legacy payload remains extractable");
    }
    // Context is bound to revisions, independently of the mutable session end time.
    ObjectRecord bound;
    {
        ObjectStore writer(root/"context", "context", true);
        ObjectSessionContext context; context.project = "Original project";
        auto s = writer.beginSession({"local:test", "editor", "Editor"}, "device", "bound", context);
        bound = writer.importFile(source, "Files/bound", s.key); writer.endSession(s.key);
        require(writer.validate(bound.key, 1), "ending a session retains valid records");
    }
    { Database db(root/"context/objects.sqlite3"); db.exec("UPDATE work_sessions SET context=replace(context,'Original project','Changed project');"); }
    { ObjectStore reader(root/"context", "context", ObjectStore::Access::ReadOnly); require(!reader.validate(bound.key, 1, false), "context edits break the validation chain"); }
    fs::remove_all(root);
    std::cout << "Journal migration, rollback, WAL readers, stable cursors and session integrity verified\n";
    return 0;
} catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
