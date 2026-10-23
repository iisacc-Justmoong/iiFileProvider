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
std::int64_t chunkCount(const fs::path &path) {
    sqlite3 *db = nullptr; const auto u = path.u8string(); sqlite3_stmt *q = nullptr;
    require(sqlite3_open_v2(reinterpret_cast<const char *>(u.c_str()), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK, "fixture database");
    require(sqlite3_prepare_v2(db, "SELECT count(*) FROM chunks", -1, &q, nullptr) == SQLITE_OK, "chunk query");
    require(sqlite3_step(q) == SQLITE_ROW, "chunk result");
    const auto result = sqlite3_column_int64(q, 0); sqlite3_finalize(q); sqlite3_close(db); return result;
}
std::size_t entries(const fs::path &path) {
    return static_cast<std::size_t>(std::distance(fs::recursive_directory_iterator(path), fs::recursive_directory_iterator{}));
}
int main() try {
    const auto root = fs::path(OBJECT_TEST_DIRECTORY) / ("metadata-limits-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    const auto source = root/"empty.raw"; { std::ofstream out(source, std::ios::binary); }
    ObjectRecord original;
    std::string session;
    {
        ObjectStore store(root/"package", "bounds", true);
        auto s = store.beginSession({"local:test", "editor", "Editor"}, "device", "limits"); session = s.key;
        ObjectSessionContext context; context.application = std::string(1, '\xff');
        rejects([&] { store.beginSession(s.author, "device", "bad UTF-8", context); });
        context = {}; context.attributes["access_token"] = "fixture";
        rejects([&] { store.beginSession(s.author, "device", "credentials", context); });
        context = {}; context.document = std::string(4097, 'x');
        rejects([&] { store.beginSession(s.author, "device", "oversize", context); });
        require(store.sessions().size() == 1, "invalid context creates no session");
        RawMetadata raw; raw.originalName = "empty.raw"; raw.annotations.fields["a/b~c"] = "before";
        original = store.wrapRaw(source, "Files/raw", raw, s.key);
        require(original.size == 0 && store.validate(original.key, 1), "empty RAW is valid");
        auto updated = raw.annotations; updated.fields.erase("a/b~c"); updated.fields["new"] = "after";
        const auto chunks = chunkCount(root/"package/objects.sqlite3");
        store.reviseMetadata(original.key, updated, 1, s.key);
        require(chunkCount(root/"package/objects.sqlite3") == chunks, "metadata creates no payload chunks");
        const auto delta = store.metadataDiff(original.key, 2);
        require(delta.size() == 2 && delta[0].pointer == "/fields/a~1b~0c" && delta[0].beforeJson == "\"before\"" && !delta[0].afterJson
            && delta[1].pointer == "/fields/new" && !delta[1].beforeJson && delta[1].afterJson == "\"after\"", "escaped JSON Pointer and absence semantics");
        auto invalid = updated; invalid.title = std::string(1, '\xff');
        rejects([&] { store.reviseMetadata(original.key, invalid, 2, s.key); });
        invalid = updated; invalid.description = std::string("a\0b", 3);
        rejects([&] { store.reviseMetadata(original.key, invalid, 2, s.key); });
        invalid = updated; invalid.color = static_cast<ColorLabel>(-1);
        rejects([&] { store.reviseMetadata(original.key, invalid, 2, s.key); });
        invalid = updated;
        for (unsigned i = 0; i != 5; ++i) invalid.fields[std::to_string(i)] = std::string(60000, 'x');
        rejects([&] { store.reviseMetadata(original.key, invalid, 2, s.key); });
        raw.originalName = "../escape.raw";
        rejects([&] { store.wrapRaw(source, "Files/invalid", raw, s.key); });
        raw.originalName = "empty.raw";
        std::stop_source stop; stop.request_stop(); ObjectWriteOptions cancelled; cancelled.cancellation = stop.get_token();
        rejects([&] { store.wrapRaw(source, "Files/cancelled", raw, s.key, cancelled); });
        rejects([&] { ObjectSessionQuery q; q.limit = 129; store.sessions(q); });
        rejects([&] { ObjectJournalQuery q; q.afterSequence = -1; store.journal(q); });
        require(store.count(true) == 1 && store.journal().size() == 2, "rejected changes leave versions and journal untouched");
        auto moved = store.move(original.key, "Files/moved.raw", 2, s.key);
        require(store.rawEnvelope(original.key).metadata.annotations == updated && store.metadataDiff(original.key, 3).empty(), "move retains RAW annotations");
        auto deleted = store.erase(original.key, 3, s.key);
        require(deleted.deleted && store.rawEnvelope(original.key).metadata.annotations == updated && store.validate(original.key, 4), "tombstone retains RAW history");
        store.extract(original.key, moved.version, root/"restored.raw");
        require(fs::file_size(root/"restored.raw") == 0, "empty historical RAW extraction");
        rejects([&] { store.extract(original.key, 4, root/"deleted.raw"); });
    }
    {
        ObjectStore reader(root/"package", "bounds", ObjectStore::Access::ReadOnly);
        const auto before = entries(root/"package"); RawMetadata raw; raw.originalName = "empty.raw";
        rejects([&] { reader.wrapRaw(source, "Files/read-only", raw, session); });
        require(entries(root/"package") == before, "read-only wrapping creates no snapshot or lease");
        rejects([&] { reader.reviseMetadata(original.key, {}, 4, session); });
        require(reader.journal().size() == 4, "read-only failures create no journal rows");
    }
    fs::remove_all(root);
    std::cout << "RAW metadata bounds, escaped diffs, cancellation and read-only safety verified\n";
    return 0;
} catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
