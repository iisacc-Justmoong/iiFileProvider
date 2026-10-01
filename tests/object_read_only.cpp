#include "ObjectStore.h"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace iiFileProvider;
namespace fs = std::filesystem;
namespace {
void require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
template<class F> void rejects(F operation) {
    bool rejected = false;
    try { operation(); } catch (const std::exception &) { rejected = true; }
    require(rejected, "read-only operation must reject mutation or invalid input");
}
struct Database {
    sqlite3 *db = nullptr;
    explicit Database(const fs::path &path) {
        const auto utf8 = path.u8string();
        require(sqlite3_open(reinterpret_cast<const char *>(utf8.c_str()), &db) == SQLITE_OK, "open fixture");
    }
    ~Database() { sqlite3_close(db); }
    void exec(const char *sql) {
        require(sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK, "fixture SQL");
    }
    int schema() {
        sqlite3_stmt *query = nullptr;
        require(sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &query, nullptr) == SQLITE_OK, "schema query");
        const auto status = sqlite3_step(query);
        const auto value = sqlite3_column_int(query, 0);
        sqlite3_finalize(query);
        require(status == SQLITE_ROW, "schema row");
        return value;
    }
};
}
int main() {
    try {
        const auto root = fs::path(OBJECT_TEST_DIRECTORY) / ("object-read-only-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directory(root);
        const auto package = root / "package";
        const auto source = root / "source";
        { std::ofstream output(source); output << "read-only fixture"; }
        rejects([&] { ObjectStore missing(package, "fixture", ObjectStore::Access::ReadOnly); });
        require(!fs::exists(package), "read-only open must not create a directory");
        fs::create_directory(root / "empty");
        rejects([&] { ObjectStore empty(root / "empty", "fixture", ObjectStore::Access::ReadOnly); });
        require(fs::is_empty(root / "empty"), "read-only open must not create a database");
        rejects([&] { ObjectStore invalid(package, "fixture", static_cast<ObjectStore::Access>(-1)); });
        require(!fs::exists(package), "invalid access must reject before creating a package");
        ObjectRecord record;
        std::string session;
        {
            ObjectStore writer(package, "fixture", ObjectStore::Access::Create);
            session = writer.beginSession({"local:test", "reader-test", "Reader test"}, "fixture", "read-only").key;
            record = writer.importFile(source, "Files/source", session);
            std::cout << "Fixture committed; checking a reader under the writer lock" << std::endl;
            Database transaction(package / "objects.sqlite3");
            transaction.exec("BEGIN IMMEDIATE");
            // Open while another connection owns the write transaction. Reading
            // must neither acquire that writer lock nor upgrade the schema.
            ObjectStore reader(package, "fixture", ObjectStore::Access::ReadOnly);
            require(reader.count() == 1 && reader.index().front().key == record.key, "concurrent index read");
            require(reader.lookupPath("Files/source")->validationKey == record.validationKey, "lookup");
            require(reader.scan().size() == 1 && reader.history(record.key).size() == 1, "record reads");
            require(reader.diff(record.key, 1).size() == 1 && reader.validate(record.key, 1), "history and payload read");
            require(reader.session(session).endedAtNs == 0, "session read");
            std::cout << "Concurrent read APIs passed; checking mutation rejection" << std::endl;
            transaction.exec("ROLLBACK");
            rejects([&] { reader.beginSession({"local:test", "invalid", "Invalid"}, "fixture", "write"); });
            rejects([&] { reader.endSession(session); });
            rejects([&] { reader.importFile(source, "Files/new", session); });
            rejects([&] { reader.importFiles({{source, "Files/batch", {}}}, session); });
            rejects([&] { reader.reviseFile(record.key, source, 1, session); });
            rejects([&] { reader.move(record.key, "Files/moved", 1, session); });
            rejects([&] { reader.erase(record.key, 1, session); });
            require(writer.count() == 1 && writer.lookup(record.key)->version == 1
                && writer.session(session).endedAtNs == 0, "failed writes preserve heads and sessions");
            std::cout << "All mutation entry points rejected; committing later state" << std::endl;
            writer.endSession(session);
            require(reader.session(session).endedAtNs != 0, "reader observes later committed state");
        }
        std::cout << "Read visibility and close passed; checking schema boundaries" << std::endl;
        rejects([&] { ObjectStore wrong(package, "different", ObjectStore::Access::ReadOnly); });
        {
            Database previous(package / "objects.sqlite3");
            previous.exec("DROP TRIGGER current_index_publish; DROP TABLE current_index; PRAGMA user_version=2;");
            rejects([&] { ObjectStore old(package, "fixture", ObjectStore::Access::ReadOnly); });
            require(previous.schema() == 2, "reader must not migrate previous schema");
        }
        std::cout << "Read-only migration rejected; checking explicit writer upgrade" << std::endl;
        {
            ObjectStore upgrade(package, "fixture", ObjectStore::Access::ReadWrite);
            require(upgrade.index().front().key == record.key, "explicit writer migrates existing package");
        }
        {
            ObjectStore reader(package, "fixture", ObjectStore::Access::ReadOnly);
            require(reader.validate(record.key, 1), "upgraded package reads with identity preserved");
        }
        fs::remove_all(root);
        std::cout << "Read-only query, concurrent writer, mutation rejection and schema boundaries passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
