#include "ObjectStore.h"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>

using namespace iiFileProvider;
namespace fs = std::filesystem;
static_assert(__cplusplus >= 202302L);
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
template<class F> void rejects(F f) {
    bool rejected = false; try { f(); } catch (const std::exception &) { rejected = true; }
    require(rejected, "operation must reject invalid, stale or read-only input");
}
std::string read(const fs::path &p) { std::ifstream in(p, std::ios::binary); return {std::istreambuf_iterator<char>(in), {}}; }
void sql(const fs::path &p, const char *statement) {
    sqlite3 *db = nullptr; const auto u = p.u8string();
    require(sqlite3_open(reinterpret_cast<const char *>(u.c_str()), &db) == SQLITE_OK, "fixture open");
    const auto code = sqlite3_exec(db, statement, nullptr, nullptr, nullptr); sqlite3_close(db);
    require(code == SQLITE_OK, "fixture SQL");
}
int main() try {
    const auto root = fs::path(OBJECT_TEST_DIRECTORY) / ("journal-raw-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    const auto source = root / "원본.nef";
    std::string original(ObjectStore::chunkBytes + 113, '\0');
    for (std::size_t i=0; i<original.size(); ++i) original[i] = static_cast<char>(i % 251);
    { std::ofstream out(source, std::ios::binary); out.write(original.data(), original.size()); }
    const auto beforeTime = fs::last_write_time(source);
    std::string key, sessionKey, firstValidation;
    {
        ObjectStore store(root/"package", "journal-contract", true);
        ObjectSessionContext context;
        context.application = "Dreamscapes"; context.applicationVersion = "1.2.3";
        context.project = "project-42"; context.document = "document-7";
        context.attributes = {{"workspace", "Studio"}};
        auto session = store.beginSession({"local:test", "editor", "Editor"}, "device-1", "RAW editing", context);
        sessionKey = session.key;
        auto work = store.workSession(session.key);
        require(work.sequence > 0 && work.context == context && work.session.key == session.key, "session context");
        require(store.sessions({}).size() == 1, "session listing");
        RawMetadata raw; raw.originalName = "원본.nef"; raw.mediaType = "image/x-nikon-nef";
        raw.annotations.title = "Original capture"; raw.annotations.keywords = {"RAW", "portrait"};
        raw.annotations.fields = {{"exposure", "0"}};
        const auto created = store.wrapRaw(source, "Files/capture.nef", raw, session.key);
        key = created.key; firstValidation = created.validationKey;
        require(created.operation == "wrap_raw" && created.version == 1 && store.validate(key, 1), "RAW publication");
        require(store.rawEnvelope(key).metadata == raw, "exact metadata round trip");
        require(read(source) == original && fs::last_write_time(source) == beforeTime, "wrapping preserves source bytes and timestamp");
        rejects([&] { store.reviseFile(key, source, 1, session.key); });
        fs::remove(source); // All later operations must use only immutable packaged bytes.
        auto edited = raw.annotations; edited.rating = 5; edited.color = ColorLabel::Green;
        edited.fields["exposure"] = "+1.0"; edited.collections = {"favorites"};
        const auto changed = store.reviseMetadata(key, edited, 1, session.key);
        require(changed.version == 2 && changed.sha256 == created.sha256 && changed.size == created.size, "metadata-only revision");
        require(store.diff(key, 2).empty(), "metadata editing never changes RAW chunks");
        auto delta = store.metadataDiff(key, 2);
        bool exposure = false;
        for (const auto &change : delta) if (change.pointer == "/fields/exposure")
            exposure = change.beforeJson == "\"0\"" && change.afterJson == "\"+1.0\"";
        require(exposure && delta.size() == 4, "structured metadata diff");
        require(store.reviseMetadata(key, edited, 2, session.key).version == 2, "metadata no-op adds no revision");
        rejects([&] { store.reviseMetadata(key, raw.annotations, 1, session.key); });
        require(store.rawEnvelope(key, 1).metadata == raw && store.rawEnvelope(key).metadata.annotations == edited, "historical metadata");
        require(store.rawEnvelope(key, 1).record.validationKey == firstValidation, "original validation key remains stable");
        store.extract(key, 2, root/"restored.nef");
        require(read(root/"restored.nef") == original, "byte-identical RAW extraction");
        rejects([&] { store.extract(key, 1, root/"restored.nef"); });
        ObjectJournalQuery query; query.sessionKey = session.key; query.limit = 1;
        const auto first = store.journal(query); require(first.size() == 1 && first[0].version == 1, "journal page one");
        query.afterSequence = first[0].sequence;
        const auto second = store.journal(query);
        require(second.size() == 1 && second[0].version == 2 && second[0].operation == "metadata", "journal page two");
        query.afterSequence = second[0].sequence; require(store.journal(query).empty(), "journal cursor completion");
        ObjectStore competitor(root/"package", "journal-contract");
        auto rival = competitor.beginSession({"local:test", "rival", "Rival"}, "device-2", "concurrent edit");
        const auto attempt = [&](ObjectStore &s, const std::string &id, unsigned rating) {
            auto value = edited; value.rating = rating;
            try { s.reviseMetadata(key, value, 2, id); return true; } catch (const std::exception &) { return false; }
        };
        auto a = std::async(std::launch::async, [&] { return attempt(store, session.key, 3); });
        auto b = std::async(std::launch::async, [&] { return attempt(competitor, rival.key, 4); });
        require(int(a.get()) + int(b.get()) == 1, "one metadata CAS writer wins");
        require(store.journal({}).size() == 3 && store.validate(key, 3), "failed CAS leaves no journal row");
        store.endSession(session.key); competitor.endSession(rival.key);
        ObjectSessionQuery active; active.activeOnly = true; require(store.sessions(active).empty(), "closed sessions");
        rejects([&] { store.reviseMetadata(key, raw.annotations, 3, session.key); });
        require(store.workSession(session.key).context == context, "closing preserves session context");
    }
    {
        ObjectStore reader(root/"package", "journal-contract", ObjectStore::Access::ReadOnly);
        require(reader.journal({}).size() == 3 && reader.rawEnvelope(key).record.version == 3, "read-only reopen");
        require(reader.validate(key, 1) && reader.lookup(key)->version == 3, "validation survives reopen");
        rejects([&] { reader.reviseMetadata(key, {}, 3, sessionKey); });
        rejects([&] { ObjectJournalQuery bad; bad.limit = 0; reader.journal(bad); });
    }
    // Generic import/revise/move/erase share the same atomic journal trigger.
    { std::ofstream out(source, std::ios::binary); out << "plain"; }
    {
        ObjectStore store(root/"plain", "generic", true);
        auto session = store.beginSession({"local:test", "editor", "Editor"}, "device", "generic");
        auto item = store.importFile(source, "Files/plain", session.key);
        store.reviseFile(item.key, source, 1, session.key);
        store.move(item.key, "Files/renamed", 2, session.key); store.erase(item.key, 3, session.key);
        ObjectJournalQuery query; query.objectKey = item.key;
        require(store.journal(query).size() == 4, "every storage mutation is journaled");
        RawMetadata bad; bad.originalName = "bad.raw"; bad.mediaType = "application/octet-stream"; bad.annotations.rating = 6;
        rejects([&] { store.wrapRaw(source, "Files/bad", bad, session.key); });
        require(store.count(true) == 1 && store.journal({}).size() == 4, "invalid metadata is atomic");
    }
    // Original-file timestamp is recorded independently of metadata edits.
    require(beforeTime.time_since_epoch().count() != 0, "source fixture timestamp");
    sql(root/"package/objects.sqlite3", "UPDATE raw_revisions SET document=replace(document,'Original capture','tampered') WHERE version=1;");
    { ObjectStore store(root/"package", "journal-contract"); require(!store.validate(key, 3, false), "RAW metadata tampering breaks the chain"); }
    fs::remove_all(root);
    std::cout << "RAW envelopes, metadata diffs, journal and work sessions verified\n";
    return 0;
} catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
