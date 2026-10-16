#include "FileCatalog.h"
#include "TemporaryLease.h"
#include "Presentation_p.h"
#include <nlohmann/json.hpp>
#include <utf8proc.h>
#include <sqlite3.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <mutex>
#include <set>
#include <stdexcept>
#include <variant>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif
#endif

namespace iiFileProvider {
namespace {
namespace fs = std::filesystem;
using Json = nlohmann::json;
using Value = std::variant<std::string, std::int64_t>;
constexpr int applicationId = 0x49494643; // IIFC; separate from object packages.
constexpr const char *privateDirectory = ".iisacc-file-provider";
constexpr const char *companionSuffix = ".iisacc.json";
constexpr std::size_t maximumBatchMetadata = 16 * 1024 * 1024;
[[noreturn]] void fail(const std::string &message) { throw std::runtime_error("iiFileProvider: " + message); }
std::uint64_t jsonInteger(const Json &value, std::uint64_t maximum = INT64_MAX) {
    if ((!value.is_number_unsigned() && !value.is_number_integer()) || (value.is_number_integer() && !value.is_number_unsigned() && value.get<std::int64_t>() < 0))
        fail("metadata number must be a nonnegative integer");
    const auto result = value.get<std::uint64_t>();
    if (result > maximum) fail("metadata number exceeds limit");
    return result;
}
std::string pathText(const fs::path &p) { const auto u = p.generic_u8string(); return {reinterpret_cast<const char *>(u.data()), u.size()}; }
fs::path nativePath(const std::string &s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::int64_t integer(std::uint64_t value) {
    if (value > std::uint64_t(INT64_MAX)) fail("integer exceeds SQLite range");
    return static_cast<std::int64_t>(value);
}
std::string normalized(const std::string &text, std::size_t maximum, bool fold = false, bool required = false) {
    if (text.size() > maximum || text.find('\0') != std::string::npos || (required && text.empty())) fail("invalid metadata string");
    utf8proc_uint8_t *output = nullptr;
    const auto options = utf8proc_option_t(UTF8PROC_STABLE | UTF8PROC_COMPOSE | (fold ? UTF8PROC_CASEFOLD : 0));
    const auto count = utf8proc_map(reinterpret_cast<const utf8proc_uint8_t *>(text.data()),
        static_cast<utf8proc_ssize_t>(text.size()), &output, options);
    std::unique_ptr<utf8proc_uint8_t, decltype(&std::free)> guard(output, &std::free);
    if (count < 0 || std::size_t(count) > maximum) fail("invalid UTF-8 metadata");
    std::string result(reinterpret_cast<const char *>(output), static_cast<std::size_t>(count));
    if (required && result.find_first_not_of(" \t\r\n") == std::string::npos) fail("empty metadata name");
    return result;
}
std::vector<std::string> terms(const std::vector<std::string> &input, bool folded = false) {
    if (input.size() > 128) fail("too many metadata terms");
    std::set<std::string> seen; std::vector<std::string> result;
    for (const auto &value : input) {
        const auto display = normalized(value, 512, false, true);
        const auto term = normalized(display, 512, true);
        if (seen.insert(term).second) result.push_back(folded ? term : display);
    }
    return result;
}
std::string relativePath(const std::string &text, bool empty = false) {
    (void)normalized(text, 4096, false, !empty);
    if (text.empty() && empty) return text;
    auto foldedPath = text;
    for (auto &c : foldedPath) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    if (text.empty() || text.front() == '/' || text.find_first_of("\\:\0", 0, 3) != std::string::npos || foldedPath.ends_with(companionSuffix) || foldedPath.ends_with(".iisacc-preview"))
        fail("invalid managed file path");
    const auto path = nativePath(text);
    if (path.is_absolute() || path.has_root_name() || pathText(path.lexically_normal()) != text) fail("noncanonical managed path");
    for (const auto &part : path) {
        const auto name = pathText(part);
        auto controlName = name;
        for (auto &c : controlName) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        const bool providerControl = controlName == ".iisacc-file-provider" || controlName == ".iisacc-temporary.lock"
            || controlName.starts_with(".iifile-stage-") || controlName.starts_with(".society-extract-");
        if (name.empty() || providerControl || name.size() > 220 || name.back() == ' ' || name.back() == '.'
            || name.find_first_of("<>\"|?*") != std::string::npos
            || std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32; }))
            fail("private or nonportable managed path");
        auto base = name.substr(0, name.find('.'));
        while (!base.empty() && base.back() == ' ') base.pop_back();
        for (auto &c : base) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        const bool port = base.starts_with("com") || base.starts_with("lpt");
        const auto suffix = port ? base.substr(3) : std::string{};
        if (base == "con" || base == "prn" || base == "aux" || base == "nul" || base == "conin$" || base == "conout$"
            || (port && ((suffix.size() == 1 && suffix[0] >= '1' && suffix[0] <= '9') || suffix == "¹" || suffix == "²" || suffix == "³")))
            fail("reserved native device name");
    }
    return text;
}
constexpr std::array<const char *, 8> colorNames{"none", "red", "orange", "yellow", "green", "blue", "purple", "gray"};
unsigned colorValue(ColorLabel color) { const auto n = static_cast<unsigned>(color); if (n >= colorNames.size()) fail("unknown color label"); return n; }
Json metadataJson(const FileMetadata &metadata) {
    return {{"title", metadata.title}, {"description", metadata.description}, {"keywords", metadata.keywords},
        {"rating", metadata.rating}, {"color", colorNames[colorValue(metadata.color)]}, {"fields", metadata.fields}, {"collections", metadata.collections}};
}
FileMetadata normalizeMetadata(FileMetadata metadata) {
    if (metadata.rating > 5) fail("rating must be between zero and five");
    (void)colorValue(metadata.color);
    metadata.title = normalized(metadata.title, 4096); metadata.description = normalized(metadata.description, 64 * 1024);
    metadata.keywords = terms(metadata.keywords);
    if (metadata.fields.size() > 64 || metadata.collections.size() > 128) fail("too many classification fields");
    std::map<std::string, std::string> fields;
    for (const auto &[key, value] : metadata.fields) {
        if (!fields.emplace(normalized(key, 256, false, true), normalized(value, 64 * 1024)).second) fail("duplicate normalized field");
    }
    metadata.fields = std::move(fields);
    std::set<std::string> memberships;
    for (const auto &key : metadata.collections) memberships.insert(normalized(key, 128, false, true));
    metadata.collections.assign(memberships.begin(), memberships.end());
    if (metadataJson(metadata).dump().size() > FileCatalog::maximumMetadataBytes) fail("file classification exceeds byte limit");
    return metadata;
}
FileMetadata parseMetadata(const Json &j) {
    if (!j.is_object()) fail("classification must be an object");
    const std::set<std::string> keys{"title", "description", "keywords", "rating", "color", "fields", "collections"};
    for (const auto &[key, value] : j.items()) if (!keys.contains(key)) fail("unknown classification field");
    FileMetadata m; m.title = j.value("title", std::string{}); m.description = j.value("description", std::string{});
    m.keywords = j.value("keywords", std::vector<std::string>{});
    if (j.contains("rating")) m.rating = static_cast<unsigned>(jsonInteger(j.at("rating"), 5));
    const auto color = j.value("color", std::string("none"));
    const auto i = std::find(colorNames.begin(), colorNames.end(), color);
    if (i == colorNames.end()) fail("unknown companion color");
    m.color = ColorLabel(i - colorNames.begin()); m.fields = j.value("fields", std::map<std::string, std::string>{});
    m.collections = j.value("collections", std::vector<std::string>{}); return normalizeMetadata(std::move(m));
}
Json filterJson(const FileFilter &f) {
    Json j{{"text", f.text}, {"pathPrefix", f.pathPrefix}, {"allKeywords", f.allKeywords}, {"anyKeywords", f.anyKeywords},
        {"colors", Json::array()}, {"collections", f.collections}, {"fields", f.fields}};
    for (auto c : f.colors) j["colors"].push_back(colorValue(c));
    if (f.minimumRating) j["minimumRating"] = *f.minimumRating;
    if (f.maximumRating) j["maximumRating"] = *f.maximumRating;
    if (f.minimumSize) j["minimumSize"] = *f.minimumSize;
    if (f.maximumSize) j["maximumSize"] = *f.maximumSize;
    return j;
}
FileFilter normalizeFilter(FileFilter f) {
    f.text = normalized(f.text, 4096); f.pathPrefix = relativePath(f.pathPrefix, true);
    f.allKeywords = terms(f.allKeywords, true); f.anyKeywords = terms(f.anyKeywords, true);
    if (f.colors.size() > 8 || f.collections.size() > 128 || f.fields.size() > 64) fail("filter exceeds field limits");
    for (const auto c : f.colors) (void)colorValue(c);
    for (auto &c : f.collections) c = normalized(c, 128, false, true);
    std::map<std::string, std::string> fields;
    for (const auto &[k, v] : f.fields)
        if (!fields.emplace(normalized(k, 256, false, true), normalized(v, 64 * 1024)).second) fail("duplicate normalized filter field");
    f.fields = std::move(fields);
    if ((f.minimumRating && *f.minimumRating > 5) || (f.maximumRating && *f.maximumRating > 5)
        || (f.minimumRating && f.maximumRating && *f.minimumRating > *f.maximumRating)
        || (f.minimumSize && f.maximumSize && *f.minimumSize > *f.maximumSize)) fail("invalid filter range");
    if (f.minimumSize) (void)integer(*f.minimumSize);
    if (f.maximumSize) (void)integer(*f.maximumSize);
    if (filterJson(f).dump().size() > FileCatalog::maximumMetadataBytes) fail("filter exceeds byte limit");
    return f;
}
FileFilter parseFilter(const std::string &text) {
    const auto j = Json::parse(text); FileFilter f;
    f.text = j.value("text", std::string{}); f.pathPrefix = j.value("pathPrefix", std::string{});
    f.allKeywords = j.value("allKeywords", std::vector<std::string>{}); f.anyKeywords = j.value("anyKeywords", std::vector<std::string>{});
    for (const auto &c : j.at("colors")) { const auto value = c.get<unsigned>(); if (value >= colorNames.size()) fail("invalid saved color"); f.colors.push_back(ColorLabel(value)); }
    f.collections = j.value("collections", std::vector<std::string>{}); f.fields = j.value("fields", std::map<std::string, std::string>{});
    if (j.contains("minimumRating")) f.minimumRating = j.at("minimumRating").get<unsigned>();
    if (j.contains("maximumRating")) f.maximumRating = j.at("maximumRating").get<unsigned>();
    if (j.contains("minimumSize")) f.minimumSize = j.at("minimumSize").get<std::uint64_t>();
    if (j.contains("maximumSize")) f.maximumSize = j.at("maximumSize").get<std::uint64_t>();
    return normalizeFilter(std::move(f));
}
std::string matchExpression(const std::string &text) {
    std::vector<std::string> words; std::string word; const auto *input = reinterpret_cast<const utf8proc_uint8_t *>(text.data());
    for (std::size_t at = 0; at < text.size();) {
        utf8proc_int32_t cp; const auto n = utf8proc_iterate(input + at, static_cast<utf8proc_ssize_t>(text.size() - at), &cp);
        if (n <= 0) fail("invalid search UTF-8");
        const auto category = utf8proc_category(cp);
        const bool part = (category >= UTF8PROC_CATEGORY_LU && category <= UTF8PROC_CATEGORY_NO);
        if (part) word.append(text, at, static_cast<std::size_t>(n));
        else if (!word.empty()) { words.push_back(std::move(word)); word.clear(); }
        at += static_cast<std::size_t>(n);
    }
    if (!word.empty()) words.push_back(std::move(word));
    if (words.empty() || words.size() > 32) fail("search needs one to 32 word prefixes");
    std::string result; for (const auto &w : words) { if (!result.empty()) result += " AND "; result += '"' + w + "\"*"; }
    return result;
}
void check(sqlite3 *db, int code) { if (code != SQLITE_OK) fail(db ? sqlite3_errmsg(db) : "SQLite unavailable"); }
void exec(sqlite3 *db, const char *sql) { check(db, sqlite3_exec(db, sql, nullptr, nullptr, nullptr)); }
class Statement {
    sqlite3 *db; sqlite3_stmt *q = nullptr;
public:
    Statement(sqlite3 *database, const std::string &sql) : db(database) { check(db, sqlite3_prepare_v2(db, sql.c_str(), -1, &q, nullptr)); }
    ~Statement() { sqlite3_finalize(q); }
    Statement(const Statement &) = delete;
    void bind(int i, const std::string &text) { if (text.size() > FileCatalog::maximumMetadataBytes + 65536) fail("SQLite text limit"); check(db, sqlite3_bind_text(q, i, text.data(), static_cast<int>(text.size()), SQLITE_TRANSIENT)); }
    void bind(int i, std::int64_t n) { check(db, sqlite3_bind_int64(q, i, n)); }
    void bind(int i, const Value &v) { std::visit([&](const auto &x) { bind(i, x); }, v); }
    bool row() { const int c = sqlite3_step(q); if (c == SQLITE_ROW) return true; if (c != SQLITE_DONE) check(db, c); return false; }
    void done() { if (row()) fail("unexpected SQLite result"); }
    std::string text(int i) const { const auto *p = sqlite3_column_text(q, i); if (!p || sqlite3_column_type(q, i) != SQLITE_TEXT) fail("invalid catalog text"); return {reinterpret_cast<const char *>(p), std::size_t(sqlite3_column_bytes(q, i))}; }
    std::int64_t number(int i) const { if (sqlite3_column_type(q, i) != SQLITE_INTEGER) fail("invalid catalog number"); return sqlite3_column_int64(q, i); }
};
struct Transaction {
    sqlite3 *db; bool committed = false;
    explicit Transaction(sqlite3 *db, bool write = true) : db(db) { exec(db, write ? "BEGIN IMMEDIATE" : "BEGIN"); }
    ~Transaction() { if (!committed) sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); }
    void commit() { exec(db, "COMMIT"); committed = true; }
};
std::int64_t scalar(sqlite3 *db, const char *sql) { Statement q(db, sql); if (!q.row()) fail("catalog scalar absent"); return q.number(0); }
std::string newKey(sqlite3 *db, const char *prefix) { Statement q(db, "SELECT lower(hex(randomblob(16)))"); if (!q.row()) fail("catalog key unavailable"); return prefix + q.text(0); }
struct Physical { std::string identity, version; std::uint64_t size; std::int64_t modified; };
Physical inspect(const fs::path &path, bool directory = false) {
    const auto status = fs::symlink_status(path);
    if (fs::is_symlink(status) || (directory ? !fs::is_directory(status) : !fs::is_regular_file(status))) fail("path is missing, redirected or not the expected file type");
#ifdef _WIN32
    const auto attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) fail("redirected Windows file");
    const auto handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, directory ? FILE_FLAG_BACKUP_SEMANTICS : 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) fail("cannot inspect native file");
    BY_HANDLE_FILE_INFORMATION i{}; const auto ok = GetFileInformationByHandle(handle, &i); CloseHandle(handle);
    if (!ok) fail("native file identity unavailable");
    const auto size = (std::uint64_t(i.nFileSizeHigh) << 32) | i.nFileSizeLow;
    const auto ticks = (std::uint64_t(i.ftLastWriteTime.dwHighDateTime) << 32) | i.ftLastWriteTime.dwLowDateTime;
    const auto modified = static_cast<std::int64_t>((ticks - 116444736000000000ull) * 100);
    const auto identity = std::to_string(i.dwVolumeSerialNumber) + ":" + std::to_string(i.nFileIndexHigh) + ":" + std::to_string(i.nFileIndexLow);
    return {identity, identity + ":" + std::to_string(size) + ":" + std::to_string(ticks), size, modified};
#else
    struct stat s{}; if (::lstat(path.c_str(), &s)) fail("cannot inspect file metadata");
#ifdef __APPLE__
    const auto modified = s.st_mtimespec; const auto changed = s.st_ctimespec;
#else
    const auto modified = s.st_mtim; const auto changed = s.st_ctim;
#endif
    if (s.st_size < 0) fail("negative native file size");
    const auto identity = std::to_string(s.st_dev) + ":" + std::to_string(s.st_ino);
    return {identity, identity + ":" + std::to_string(s.st_size) + ":" + std::to_string(modified.tv_sec) + ":" + std::to_string(modified.tv_nsec)
        + ":" + std::to_string(changed.tv_sec) + ":" + std::to_string(changed.tv_nsec), std::uint64_t(s.st_size), modified.tv_sec * 1000000000ll + modified.tv_nsec};
#endif
}
std::string smallRead(const fs::path &path) {
    const auto state = inspect(path); if (state.size > FileCatalog::maximumMetadataBytes) fail("companion exceeds metadata limit");
    std::ifstream input(path, std::ios::binary); std::string bytes(static_cast<std::size_t>(state.size), '\0');
    if (!input || !input.read(bytes.data(), static_cast<std::streamsize>(bytes.size())) || input.peek() != std::char_traits<char>::eof()) fail("cannot read stable metadata companion");
    if (inspect(path).version != state.version) fail("companion changed while reading"); return bytes;
}
Json companion(const fs::path &path) {
    auto j = Json::parse(smallRead(path));
    if (!j.is_object() || j.value("format", std::string{}) != "iiFileProvider.Metadata" || j.value("schema", 0) != 1) fail("unknown metadata companion format");
    const auto key = j.at("fileKey").get<std::string>();
    if (key.size() != 37 || !key.starts_with("file-") || key.substr(5).find_first_not_of("0123456789abcdef") != std::string::npos) fail("invalid persistent file key");
    if (jsonInteger(j.at("revision")) == 0) fail("invalid companion revision");
    const auto metadata = parseMetadata(j.at("metadata"));
    const auto &collections = j.at("collections");
    if (!collections.is_array() || collections.size() != metadata.collections.size()) fail("invalid companion collections");
    std::set<std::string> keys;
    for (const auto &c : collections) {
        const auto id = normalized(c.at("key").get<std::string>(), 128, false, true);
        (void)normalized(c.at("name").get<std::string>(), 512, false, true);
        if (!keys.insert(id).second || std::find(metadata.collections.begin(), metadata.collections.end(), id) == metadata.collections.end()) fail("inconsistent companion membership");
    }
    return j;
}
void syncFile(const fs::path &path) {
#ifdef _WIN32
    const auto h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) fail("cannot flush staged file"); const auto ok = FlushFileBuffers(h); CloseHandle(h); if (!ok) fail("cannot flush staged file");
#else
    const auto fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW); if (fd < 0) fail("cannot flush staged file");
    const auto result = ::fsync(fd); ::close(fd); if (result) fail("cannot flush staged file");
#endif
}
void syncDirectory(const fs::path &directory) {
#ifndef _WIN32
    const auto fd = ::open(directory.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_DIRECTORY);
    if (fd < 0) fail("cannot flush publication directory");
    const auto result = ::fsync(fd); const auto error = errno; ::close(fd);
    // Some filesystems provide atomic rename without directory fsync support.
    if (result && error != EINVAL && error != ENOTSUP) fail("cannot flush publication directory");
#endif
}
class Stage {
    TemporaryLease lease;
public:
    fs::path path;
    Stage(const fs::path &parent, const std::string &nonce) : lease(parent, TemporaryLease::Mode::Shared), path(parent / (".iifile-stage-" + nonce)) {
#ifdef _WIN32
        const auto h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) fail("cannot reserve staged file"); CloseHandle(h);
#else
        const auto fd = ::open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd < 0) fail("cannot reserve staged file"); ::close(fd);
#endif
    }
    ~Stage() { if (!path.empty()) { std::error_code ec; fs::remove(path, ec); } }
    void publish(const fs::path &destination, bool replace) {
        syncFile(path);
        if (replace && fs::exists(fs::symlink_status(destination)) && !fs::is_regular_file(fs::symlink_status(destination))) fail("metadata destination is redirected or not a file");
#ifdef _WIN32
        if (!MoveFileExW(path.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0))) fail("native publication failed or destination already exists");
#elif defined(__APPLE__)
        if ((replace ? ::rename(path.c_str(), destination.c_str()) : ::renamex_np(path.c_str(), destination.c_str(), RENAME_EXCL)) != 0) fail("native publication failed or destination already exists");
#else
        if (replace) { if (::rename(path.c_str(), destination.c_str())) fail("native replacement failed"); }
        else {
            // Hard-link creation is exclusive and cannot replace a preexisting
            // destination. Staging is in the destination filesystem.
            if (::link(path.c_str(), destination.c_str())) fail("exclusive publication failed or is unsupported on this filesystem");
            if (::unlink(path.c_str())) fail("cannot remove published staging name");
        }
#endif
        path.clear();
        syncDirectory(destination.parent_path());
    }
};
void writeBytes(const fs::path &path, std::span<const std::byte> bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc); if (!output) fail("cannot open staged output");
    for (std::size_t at = 0; at < bytes.size();) { const auto n = std::min(FileCatalog::maximumChunk, bytes.size() - at); output.write(reinterpret_cast<const char *>(bytes.data() + at), static_cast<std::streamsize>(n)); if (!output) fail("cannot write staged output"); at += n; }
    output.flush(); if (!output) fail("cannot flush staged output");
}
std::span<const std::byte> asBytes(const std::string &text) { return {reinterpret_cast<const std::byte *>(text.data()), text.size()}; }
void requireVacant(const fs::path &path) {
    const auto presentation = path.parent_path() / nativePath("." + pathText(path.filename()) + ".iisacc-preview");
    if (fs::exists(fs::symlink_status(path)) || fs::exists(fs::symlink_status(nativePath(pathText(path) + companionSuffix)))
        || fs::exists(fs::symlink_status(presentation)))
        fail("file or companion already exists");
}
} // namespace

class FileCatalog::Impl {
public:
    fs::path root, state;
    std::string rootIdentity, stateIdentity, databaseIdentity, catalogId;
    sqlite3 *db = nullptr;
    bool readOnly;
    mutable std::mutex mutex;
    Impl(const fs::path &input, Access access) : readOnly(access == Access::ReadOnly) {
        if (input.empty() || !input.is_absolute()) fail("catalog root must be an absolute local directory");
        if (access == Access::Create && !fs::exists(fs::symlink_status(input))) fs::create_directories(input);
        (void)inspect(input, true); root = fs::canonical(input); rootIdentity = inspect(root, true).identity;
        state = root / privateDirectory;
        if (access == Access::Create && !fs::exists(fs::symlink_status(state))) { fs::create_directory(state); fs::permissions(state, fs::perms::owner_all); }
        stateIdentity = inspect(state, true).identity;
        const auto path = state / "catalog.sqlite3"; const bool fresh = !fs::exists(fs::symlink_status(path));
        if (fresh && access != Access::Create) fail("classification catalog does not exist");
        if (!fresh) (void)inspect(path);
        for (const auto suffix : {"-wal", "-shm", "-journal"}) {
            const auto auxiliary = nativePath(pathText(path) + suffix);
            if (fs::exists(fs::symlink_status(auxiliary))) (void)inspect(auxiliary);
        }
        const int flags = readOnly ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE | (access == Access::Create ? SQLITE_OPEN_CREATE : 0);
        const auto code = sqlite3_open_v2(pathText(path).c_str(), &db, flags | SQLITE_OPEN_NOMUTEX, nullptr);
        try {
            // Concurrent native writers may wait through durable filesystem
            // flushes on removable storage; revision checks still run after
            // admission, so waiting never authorizes a stale metadata edit.
            check(db, code); check(db, sqlite3_busy_timeout(db, 30000)); exec(db, "PRAGMA foreign_keys=ON;");
            if (!readOnly) {
                // Recheck under the writer lock: independent app processes may
                // create the same initially empty catalog concurrently.
                Transaction transaction(db);
                const auto app = scalar(db, "PRAGMA application_id"), version = scalar(db, "PRAGMA user_version");
                const bool initialize = app == 0 && version == 0 && scalar(db, "SELECT count(*) FROM sqlite_master WHERE name NOT LIKE 'sqlite_%'") == 0;
                if (initialize && access != Access::Create) fail("classification schema does not exist");
                if (!initialize && (app != applicationId || version != 1)) fail("unknown classification catalog");
                if (initialize) {
                exec(db, R"SQL(
CREATE TABLE catalog(id TEXT NOT NULL,revision INTEGER NOT NULL CHECK(typeof(revision)='integer' AND revision>=0));
CREATE TABLE files(id INTEGER PRIMARY KEY AUTOINCREMENT,key TEXT UNIQUE NOT NULL,path TEXT UNIQUE NOT NULL,identity TEXT UNIQUE NOT NULL,
 size INTEGER NOT NULL CHECK(size>=0),modified INTEGER NOT NULL,stamp TEXT NOT NULL,revision INTEGER NOT NULL CHECK(typeof(revision)='integer' AND revision>0),
 rating INTEGER NOT NULL CHECK(rating BETWEEN 0 AND 5),color INTEGER NOT NULL CHECK(color BETWEEN 0 AND 7),title TEXT NOT NULL,metadata TEXT NOT NULL);
CREATE INDEX file_rating ON files(rating,id); CREATE INDEX file_color ON files(color,id); CREATE INDEX file_size ON files(size,id);
CREATE INDEX file_path ON files(path,id);
CREATE TABLE keywords(file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,term TEXT NOT NULL,PRIMARY KEY(file_id,term)) WITHOUT ROWID;
CREATE INDEX keyword_lookup ON keywords(term,file_id);
CREATE TABLE fields(file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,name TEXT NOT NULL,value TEXT NOT NULL,PRIMARY KEY(file_id,name)) WITHOUT ROWID;
CREATE INDEX field_lookup ON fields(name,value,file_id);
CREATE TABLE collections(key TEXT PRIMARY KEY,name TEXT NOT NULL,folded TEXT UNIQUE NOT NULL);
CREATE TABLE members(file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,collection_key TEXT NOT NULL REFERENCES collections(key) ON DELETE CASCADE,PRIMARY KEY(file_id,collection_key)) WITHOUT ROWID;
CREATE INDEX member_lookup ON members(collection_key,file_id);
CREATE TABLE saved_filters(key TEXT PRIMARY KEY,name TEXT NOT NULL,folded TEXT UNIQUE NOT NULL,filter TEXT NOT NULL);
CREATE TABLE pending(file_id INTEGER PRIMARY KEY REFERENCES files(id) ON DELETE CASCADE);
CREATE VIRTUAL TABLE search_fts USING fts5(path,title,description,keywords,fields,tokenize='unicode61 remove_diacritics 2',prefix='2 3 4');
PRAGMA application_id=0x49494643; PRAGMA user_version=1;
)SQL");
                Statement insert(db, "INSERT INTO catalog VALUES(?,0)"); insert.bind(1, newKey(db, "catalog-")); insert.done();
                }
                transaction.commit();
                exec(db, "PRAGMA journal_mode=WAL;");
            }
            if (scalar(db, "PRAGMA application_id") != applicationId || scalar(db, "PRAGMA user_version") != 1) fail("unknown classification catalog");
            if (!readOnly) exec(db, "PRAGMA synchronous=FULL;"); else exec(db, "PRAGMA query_only=ON;");
            Statement id(db, "SELECT id FROM catalog"); if (!id.row()) fail("catalog identity absent"); catalogId = id.text(0);
            databaseIdentity = inspect(path).identity;
        } catch (...) { if (db) sqlite3_close(db); db = nullptr; throw; }
    }
    ~Impl() { if (db) sqlite3_close(db); }
    void intact() const {
        if (inspect(root, true).identity != rootIdentity || inspect(state, true).identity != stateIdentity
            || inspect(state / "catalog.sqlite3").identity != databaseIdentity) fail("catalog root or database was replaced");
    }
    void writable() const { intact(); if (readOnly) fail("classification catalog is read-only"); }
    fs::path path(const std::string &relative, bool parents = false) const {
        intact(); const auto value = relativePath(relative); fs::path at = root;
        const auto rel = nativePath(value);
        for (auto i = rel.begin(); i != rel.end(); ++i) {
            at /= *i; auto next = i; ++next; const bool parent = next != rel.end();
            auto status = fs::symlink_status(at);
            if (parent && !fs::exists(status) && parents) { fs::create_directory(at); status = fs::symlink_status(at); }
            if (fs::is_symlink(status) || (parent && !fs::is_directory(status))) fail("file path is missing or redirected");
#ifdef _WIN32
            const auto attr = GetFileAttributesW(at.c_str()); if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_REPARSE_POINT)) fail("redirected Windows path");
#endif
        }
        return at;
    }
    void bump() { exec(db, "UPDATE catalog SET revision=revision+1"); }
    CatalogFile entry(Statement &q) const {
        CatalogFile f; f.index = q.number(0); f.key = q.text(1); f.path = q.text(2); f.size = std::uint64_t(q.number(3));
        f.modifiedAtNs = q.number(4); f.contentVersion = q.text(5); f.revision = std::uint64_t(q.number(6));
        f.metadata = parseMetadata(Json::parse(q.text(7))); f.metadata.collections.clear(); f.metadataPending = q.number(8) != 0;
        Statement memberships(db, "SELECT collection_key FROM members WHERE file_id=? ORDER BY collection_key"); memberships.bind(1, f.index);
        while (memberships.row()) f.metadata.collections.push_back(memberships.text(0)); return f;
    }
    static std::string columns() { return "e.id,e.key,e.path,e.size,e.modified,e.stamp,e.revision,e.metadata,EXISTS(SELECT 1 FROM pending p WHERE p.file_id=e.id)"; }
    std::optional<CatalogFile> find(const std::string &value, bool byPath = false) const {
        Statement q(db, "SELECT " + columns() + " FROM files e WHERE " + (byPath ? "e.path=?" : "e.key=?")); q.bind(1, value);
        if (!q.row()) return {}; return entry(q);
    }
    CatalogFile require(const std::string &key) const { const auto value = find(normalized(key, 128, false, true)); if (!value) fail("file key is not registered"); return *value; }
    void annotations(std::int64_t id, const std::string &path, const FileMetadata &metadata) {
        auto j = metadataJson(metadata); j["collections"] = Json::array();
        Statement update(db, "UPDATE files SET rating=?,color=?,title=?,metadata=? WHERE id=?"); update.bind(1, std::int64_t(metadata.rating)); update.bind(2, std::int64_t(colorValue(metadata.color))); update.bind(3, metadata.title); update.bind(4, j.dump()); update.bind(5, id); update.done();
        for (const auto *table : {"keywords", "fields", "members"}) { Statement remove(db, "DELETE FROM " + std::string(table) + " WHERE file_id=?"); remove.bind(1, id); remove.done(); }
        for (const auto &term : metadata.keywords) { Statement add(db, "INSERT INTO keywords VALUES(?,?)"); add.bind(1, id); add.bind(2, normalized(term, 512, true)); add.done(); }
        for (const auto &[key, value] : metadata.fields) { Statement add(db, "INSERT INTO fields VALUES(?,?,?)"); add.bind(1, id); add.bind(2, key); add.bind(3, value); add.done(); }
        for (const auto &key : metadata.collections) { Statement add(db, "INSERT INTO members VALUES(?,?)"); add.bind(1, id); add.bind(2, key); add.done(); }
        Statement remove(db, "DELETE FROM search_fts WHERE rowid=?"); remove.bind(1, id); remove.done();
        std::string keywords, fields; for (const auto &t : metadata.keywords) keywords += t + '\n'; for (const auto &[k, v] : metadata.fields) fields += k + '\n' + v + '\n';
        Statement text(db, "INSERT INTO search_fts(rowid,path,title,description,keywords,fields) VALUES(?,?,?,?,?,?)");
        text.bind(1, id); text.bind(2, path); text.bind(3, metadata.title); text.bind(4, metadata.description); text.bind(5, keywords); text.bind(6, fields); text.done();
        Statement pending(db, "INSERT OR IGNORE INTO pending VALUES(?)"); pending.bind(1, id); pending.done();
    }
    std::string document(const CatalogFile &f) const {
        Json collections = Json::array();
        for (const auto &key : f.metadata.collections) { Statement c(db, "SELECT name FROM collections WHERE key=?"); c.bind(1, key); if (c.row()) collections.push_back({{"key", key}, {"name", c.text(0)}}); }
        return Json{{"format", "iiFileProvider.Metadata"}, {"schema", 1}, {"fileKey", f.key}, {"path", f.path}, {"revision", f.revision},
            {"metadata", metadataJson(f.metadata)}, {"collections", collections}}.dump();
    }
    bool publish(const std::string &key) {
        writable(); Transaction transaction(db); const auto f = require(key); if (!f.metadataPending) { transaction.commit(); return false; }
        const auto file = path(f.path); const auto physical = inspect(file);
        if (physical.version != f.contentVersion) fail("file changed before companion publication; register it again");
        const auto target = nativePath(pathText(file) + companionSuffix); const auto status = fs::symlink_status(target);
        if (fs::exists(status)) {
            const auto previous = companion(target);
            if (previous.at("fileKey").get<std::string>() != key || previous.at("revision").get<std::uint64_t>() > f.revision) fail("metadata companion belongs to a different or newer file revision");
        }
        const auto json = document(f); if (json.size() > FileCatalog::maximumMetadataBytes) fail("companion exceeds byte limit");
        Stage staged(file.parent_path(), newKey(db, "")); writeBytes(staged.path, asBytes(json)); staged.publish(target, fs::exists(status));
        Statement done(db, "DELETE FROM pending WHERE file_id=?"); done.bind(1, f.index); done.done(); transaction.commit(); return true;
    }
    MetadataWrite publishResult(const std::string &key) {
        std::string error; try { (void)publish(key); } catch (const std::exception &e) { error = e.what(); }
        auto f = require(key); return {f, !f.metadataPending, std::move(error)};
    }
    void restoreCollections(const Json &j, FileMetadata &metadata) {
        std::map<std::string, std::string> remap;
        for (const auto &c : j.at("collections")) {
            const auto ckey = normalized(c.at("key").get<std::string>(), 128, false, true);
            const auto name = normalized(c.at("name").get<std::string>(), 512, false, true);
            Statement current(db, "SELECT key FROM collections WHERE key=? OR folded=? ORDER BY key=? DESC");
            current.bind(1, ckey); current.bind(2, normalized(name, 512, true)); current.bind(3, ckey);
            if (current.row()) remap[ckey] = current.text(0);
            else {
                if (scalar(db, "SELECT count(*) FROM collections") >= 4096) fail("collection count exceeds limit");
                Statement insert(db, "INSERT INTO collections VALUES(?,?,?)"); insert.bind(1, ckey); insert.bind(2, name); insert.bind(3, normalized(name, 512, true)); insert.done(); remap[ckey] = ckey;
            }
        }
        for (auto &c : metadata.collections) { if (!remap.contains(c)) fail("companion has unknown collection membership"); c = remap.at(c); }
        metadata = normalizeMetadata(std::move(metadata));
    }
    CatalogFile registration(const FileRegistration &registration) {
        const auto relative = relativePath(registration.path); const auto physical = inspect(path(relative));
        const auto existing = find(relative, true); FileMetadata metadata;
        std::string key = existing ? existing->key : newKey(db, "file-"); std::uint64_t revision = existing ? existing->revision + 1 : 1;
        if (registration.metadata) metadata = normalizeMetadata(*registration.metadata);
        else if (existing) metadata = existing->metadata;
        else {
            const auto sidecar = nativePath(pathText(path(relative)) + companionSuffix);
            if (fs::exists(fs::symlink_status(sidecar))) {
                const auto j = companion(sidecar); key = j.at("fileKey").get<std::string>(); metadata = parseMetadata(j.at("metadata"));
                revision = std::max(std::uint64_t(1), j.at("revision").get<std::uint64_t>());
                restoreCollections(j, metadata);
            }
        }
        Statement write(db, existing ? "UPDATE files SET identity=?,size=?,modified=?,stamp=?,revision=? WHERE id=?"
            : "INSERT INTO files(identity,size,modified,stamp,revision,key,path,rating,color,title,metadata) VALUES(?,?,?,?,?,?,?,0,0,'','{}')");
        write.bind(1, physical.identity); write.bind(2, integer(physical.size)); write.bind(3, physical.modified); write.bind(4, physical.version); write.bind(5, integer(revision));
        if (existing) write.bind(6, existing->index); else { write.bind(6, key); write.bind(7, relative); } write.done();
        const auto id = existing ? existing->index : sqlite3_last_insert_rowid(db); annotations(id, relative, metadata);
        const auto result = require(key); if (document(result).size() > FileCatalog::maximumMetadataBytes) fail("encoded companion exceeds byte limit"); return result;
    }
};

FileCatalog::FileCatalog(const fs::path &root, Access access) : d(std::make_unique<Impl>(root, access)) {}
FileCatalog::~FileCatalog() = default;
const fs::path &FileCatalog::root() const noexcept { return d->root; }
std::vector<CatalogFile> FileCatalog::registerFiles(const std::vector<FileRegistration> &input) {
    if (input.empty() || input.size() > maximumBatch) fail("registration batch exceeds file count limit");
    std::size_t bytes = 0; std::set<std::string> paths;
    for (const auto &f : input) { (void)relativePath(f.path); if (!paths.insert(f.path).second) fail("duplicate path in registration batch"); if (f.metadata) bytes += metadataJson(normalizeMetadata(*f.metadata)).dump().size(); if (bytes > maximumBatchMetadata) fail("registration metadata exceeds batch limit"); }
    bytes = 0;
    std::lock_guard lock(d->mutex); d->writable(); Transaction transaction(d->db); std::vector<CatalogFile> result;
    for (const auto &f : input) { result.push_back(d->registration(f)); bytes += metadataJson(result.back().metadata).dump().size(); if (bytes > maximumBatchMetadata) fail("registration metadata exceeds batch limit"); }
    d->bump(); transaction.commit(); return result;
}
MetadataWrite FileCatalog::registerFile(const std::string &path, const std::optional<FileMetadata> &metadata) {
    if (metadata) (void)normalizeMetadata(*metadata);
#ifdef IIFILEPROVIDER_HAS_PRESENTATION
    // Native attribute publication changes ctime. Enrich before registering the
    // physical version, so the catalog's strict conflict checks remain valid.
    { std::lock_guard lock(d->mutex); try { (void)native::Presentation::prepare(d->path(path)); } catch (...) {} }
#endif
    const auto registered = registerFiles({{path, metadata}}).front(); std::lock_guard lock(d->mutex);
    return d->publishResult(registered.key);
}
MetadataWrite FileCatalog::createFile(const std::string &relative, std::span<const std::byte> bytes, const FileMetadata &input) {
    return createFileWith(relative, [&](std::ostream &output) {
        for (std::size_t at = 0; at < bytes.size();) {
            const auto n = std::min(maximumChunk, bytes.size() - at);
            output.write(reinterpret_cast<const char *>(bytes.data() + at), static_cast<std::streamsize>(n));
            if (!output) fail("cannot encode staged output"); at += n;
        }
    }, input);
}
MetadataWrite FileCatalog::createFileWith(const std::string &relative, const std::function<void(std::ostream &)> &encode, const FileMetadata &input) {
    if (!encode) fail("a file encoder is required");
    const auto metadata = normalizeMetadata(input); std::lock_guard lock(d->mutex); d->writable();
    const auto destination = d->path(relative, true); requireVacant(destination);
    Stage stage(destination.parent_path(), newKey(d->db, ""));
    { std::ofstream output(stage.path, std::ios::binary | std::ios::trunc); if (!output) fail("cannot open staged encoder output"); encode(output); output.flush(); if (!output) fail("cannot finish staged encoder output"); }
    Transaction transaction(d->db);
    requireVacant(destination);
    stage.publish(destination, false); std::optional<CatalogFile> registered;
#ifdef IIFILEPROVIDER_HAS_PRESENTATION
    try { (void)native::Presentation::prepare(destination); } catch (...) {}
#endif
    try { registered = d->registration({relative, metadata}); d->bump(); transaction.commit(); }
    catch (...) { std::error_code error; fs::remove(destination, error); detail::removePresentation(destination); throw; }
    return d->publishResult(registered->key);
}
MetadataWrite FileCatalog::importFile(const fs::path &source, const std::string &relative, const std::optional<FileMetadata> &input) {
    auto metadata = input ? normalizeMetadata(*input) : FileMetadata{}; const auto before = inspect(source);
    std::optional<Json> sourceMetadata;
    const auto sourceCompanion = nativePath(pathText(source) + companionSuffix);
    if (!input && fs::exists(fs::symlink_status(sourceCompanion))) { sourceMetadata = companion(sourceCompanion); metadata = parseMetadata(sourceMetadata->at("metadata")); }
    std::lock_guard lock(d->mutex); d->writable(); const auto destination = d->path(relative, true);
    requireVacant(destination);
    Stage stage(destination.parent_path(), newKey(d->db, ""));
    { std::ifstream in(source, std::ios::binary); std::ofstream out(stage.path, std::ios::binary | std::ios::trunc); if (!in || !out) fail("cannot open file import streams");
      std::vector<char> buffer(maximumChunk); while (in) { in.read(buffer.data(), static_cast<std::streamsize>(buffer.size())); const auto count = in.gcount(); if (count) out.write(buffer.data(), count); if (!out) fail("file import output failed"); }
      if (!in.eof()) fail("file import input failed"); out.flush(); if (!out) fail("cannot flush file import"); }
    if (inspect(source).version != before.version) fail("source changed during file import");
    Transaction transaction(d->db);
    requireVacant(destination);
    if (sourceMetadata) d->restoreCollections(*sourceMetadata, metadata);
    stage.publish(destination, false); std::optional<CatalogFile> registered;
#ifdef IIFILEPROVIDER_HAS_PRESENTATION
    try { (void)native::Presentation::prepare(destination); } catch (...) {}
#endif
    try { registered = d->registration({relative, metadata}); d->bump(); transaction.commit(); }
    catch (...) { std::error_code error; fs::remove(destination, error); detail::removePresentation(destination); throw; }
    return d->publishResult(registered->key);
}
MetadataWrite FileCatalog::updateMetadata(const std::string &key, std::uint64_t expected, const FileMetadata &input) {
    const auto metadata = normalizeMetadata(input); std::lock_guard lock(d->mutex); d->writable();
    { Transaction transaction(d->db); const auto file = d->require(key);
      if (file.revision != expected) fail("classification revision conflict");
      if (inspect(d->path(file.path)).version != file.contentVersion) fail("file changed; register its current version before editing metadata");
      Statement update(d->db, "UPDATE files SET revision=revision+1 WHERE id=?"); update.bind(1, file.index); update.done();
      d->annotations(file.index, file.path, metadata);
      if (d->document(d->require(key)).size() > maximumMetadataBytes) fail("encoded companion exceeds byte limit");
      d->bump(); transaction.commit(); }
    return d->publishResult(key);
}
std::optional<CatalogFile> FileCatalog::lookup(const std::string &key) const {
    std::lock_guard lock(d->mutex); d->intact(); Transaction transaction(d->db, false); auto result = d->find(normalized(key, 128, false, true)); transaction.commit(); return result;
}
std::optional<CatalogFile> FileCatalog::lookupPath(const std::string &path) const {
    const auto relative = relativePath(path); std::lock_guard lock(d->mutex); d->intact(); Transaction transaction(d->db, false); auto result = d->find(relative, true); transaction.commit(); return result;
}
FileChunk FileCatalog::read(const std::string &key, std::uint64_t offset, std::size_t maximumBytes, const std::string &expected) const {
    if (!maximumBytes || maximumBytes > maximumChunk) fail("file provision exceeds chunk limit");
    std::lock_guard lock(d->mutex); d->intact(); const auto record = d->require(key); const auto path = d->path(record.path); const auto before = inspect(path);
    Statement identity(d->db, "SELECT identity FROM files WHERE id=?"); identity.bind(1, record.index);
    if (!identity.row() || identity.text(0) != before.identity) fail("file identity changed; register it again");
    if ((!expected.empty() && expected != before.version) || offset > before.size || offset > std::uint64_t(INT64_MAX)) fail("file content version or range conflict");
    FileChunk result; result.offset = offset; result.size = before.size; result.contentVersion = before.version;
    result.bytes.resize(static_cast<std::size_t>(std::min<std::uint64_t>(maximumBytes, before.size - offset)));
    std::ifstream stream(path, std::ios::binary); stream.seekg(static_cast<std::streamoff>(offset));
    if (!stream || !stream.read(reinterpret_cast<char *>(result.bytes.data()), static_cast<std::streamsize>(result.bytes.size()))) fail("cannot provide stable file range");
    if (inspect(path).version != before.version) fail("file changed during provision"); result.eof = offset + result.bytes.size() == before.size; return result;
}
FilePage FileCatalog::search(const FileFilter &input, const std::optional<FileCursor> &cursor, std::size_t limit) const {
    if (!limit || limit > maximumPage) fail("search page exceeds limit"); const auto filter = normalizeFilter(input); const auto signature = filterJson(filter).dump();
    std::lock_guard lock(d->mutex); d->intact(); Transaction transaction(d->db, false); const auto revision = std::uint64_t(scalar(d->db, "SELECT revision FROM catalog"));
    if (cursor && (cursor->catalog != d->catalogId || cursor->revision != revision || cursor->filter != signature || cursor->afterIndex < 0)) fail("search cursor expired or belongs to another filter/catalog");
    std::string sql = "SELECT e.id,e.key,e.path,e.size,e.modified,e.stamp,e.revision,e.title,e.rating,e.color,EXISTS(SELECT 1 FROM pending p WHERE p.file_id=e.id) FROM files e WHERE e.id>?";
    std::vector<Value> args{cursor ? cursor->afterIndex : std::int64_t(0)};
    if (!filter.text.empty()) { sql += " AND e.id IN(SELECT rowid FROM search_fts WHERE search_fts MATCH ?)"; args.emplace_back(matchExpression(filter.text)); }
    if (!filter.pathPrefix.empty()) { sql += " AND (e.path=? OR (e.path>=? AND e.path<?))"; args.emplace_back(filter.pathPrefix); args.emplace_back(filter.pathPrefix + '/'); args.emplace_back(filter.pathPrefix + '0'); }
    const auto range = [&](const char *column, const auto &minimum, const auto &maximum) { if (minimum) { sql += std::string(" AND ") + column + ">=?"; args.emplace_back(integer(*minimum)); } if (maximum) { sql += std::string(" AND ") + column + "<=?"; args.emplace_back(integer(*maximum)); } };
    range("e.rating", filter.minimumRating, filter.maximumRating); range("e.size", filter.minimumSize, filter.maximumSize);
    for (const auto &term : filter.allKeywords) { sql += " AND e.id IN(SELECT file_id FROM keywords WHERE term=?)"; args.emplace_back(term); }
    const auto selected = [&](const std::string &prefix, const auto &values, auto convert) { if (values.empty()) return; sql += prefix + '('; bool first = true; for (const auto &v : values) { if (!first) sql += ','; first = false; sql += '?'; args.emplace_back(convert(v)); } sql += "))"; };
    selected(" AND e.id IN(SELECT file_id FROM keywords WHERE term IN", filter.anyKeywords, [](const auto &v) { return Value(v); });
    selected(" AND e.id IN(SELECT file_id FROM members WHERE collection_key IN", filter.collections, [](const auto &v) { return Value(v); });
    if (!filter.colors.empty()) { sql += " AND ("; bool first = true; for (auto c : filter.colors) { if (!first) sql += " OR "; first = false; sql += "e.color=?"; args.emplace_back(std::int64_t(colorValue(c))); } sql += ')'; }
    for (const auto &[key, value] : filter.fields) { sql += " AND e.id IN(SELECT file_id FROM fields WHERE name=? AND value=?)"; args.emplace_back(key); args.emplace_back(value); }
    sql += " ORDER BY e.id LIMIT ?"; args.emplace_back(std::int64_t(limit + 1)); Statement query(d->db, sql);
    int at = 1; for (const auto &arg : args) query.bind(at++, arg);
    FilePage page;
    while (query.row()) {
        CatalogFileSummary item; item.index = query.number(0); item.key = query.text(1); item.path = query.text(2);
        item.size = std::uint64_t(query.number(3)); item.modifiedAtNs = query.number(4); item.contentVersion = query.text(5);
        item.revision = std::uint64_t(query.number(6)); item.title = query.text(7); item.rating = static_cast<unsigned>(query.number(8));
        item.color = ColorLabel(query.number(9)); item.metadataPending = query.number(10) != 0; page.files.push_back(std::move(item));
    }
    if (page.files.size() > limit) { page.files.resize(limit); page.next = FileCursor{d->catalogId, revision, page.files.back().index, signature}; }
    transaction.commit(); return page;
}
std::uint64_t FileCatalog::count() const { std::lock_guard lock(d->mutex); d->intact(); return std::uint64_t(scalar(d->db, "SELECT count(*) FROM files")); }
FileCollection FileCatalog::createCollection(const std::string &input) {
    const auto name = normalized(input, 512, false, true); std::lock_guard lock(d->mutex); d->writable(); Transaction transaction(d->db);
    if (scalar(d->db, "SELECT count(*) FROM collections") >= 4096) fail("collection count exceeds limit");
    FileCollection result{newKey(d->db, "collection-"), name}; Statement q(d->db, "INSERT INTO collections VALUES(?,?,?)"); q.bind(1, result.key); q.bind(2, name); q.bind(3, normalized(name, 512, true)); q.done(); d->bump(); transaction.commit(); return result;
}
void FileCatalog::renameCollection(const std::string &key, const std::string &input) {
    const auto name = normalized(input, 512, false, true); std::lock_guard lock(d->mutex); d->writable();
    { Transaction transaction(d->db); Statement q(d->db, "UPDATE collections SET name=?,folded=? WHERE key=?"); q.bind(1, name); q.bind(2, normalized(name, 512, true)); q.bind(3, key); q.done(); if (!sqlite3_changes(d->db)) fail("collection does not exist");
      Statement update(d->db, "UPDATE files SET revision=revision+1 WHERE id IN(SELECT file_id FROM members WHERE collection_key=?)"); update.bind(1, key); update.done();
      Statement pending(d->db, "INSERT OR IGNORE INTO pending SELECT file_id FROM members WHERE collection_key=?"); pending.bind(1, key); pending.done();
      Statement affected(d->db, "SELECT key FROM files WHERE id IN(SELECT file_id FROM members WHERE collection_key=?)"); affected.bind(1, key);
      while (affected.row()) if (d->document(d->require(affected.text(0))).size() > maximumMetadataBytes) fail("renamed collection exceeds companion byte limit");
      d->bump(); transaction.commit(); }
}
void FileCatalog::removeCollection(const std::string &key) {
    { std::lock_guard lock(d->mutex); d->writable(); Transaction transaction(d->db);
      Statement update(d->db, "UPDATE files SET revision=revision+1 WHERE id IN(SELECT file_id FROM members WHERE collection_key=?)"); update.bind(1, key); update.done();
      Statement pending(d->db, "INSERT OR IGNORE INTO pending SELECT file_id FROM members WHERE collection_key=?"); pending.bind(1, key); pending.done();
      Statement remove(d->db, "DELETE FROM collections WHERE key=?"); remove.bind(1, key); remove.done(); if (!sqlite3_changes(d->db)) fail("collection does not exist"); d->bump(); transaction.commit(); }
    (void)publishMetadata();
}
std::vector<FileCollection> FileCatalog::collections() const {
    std::lock_guard lock(d->mutex); d->intact(); Statement query(d->db, "SELECT key,name FROM collections ORDER BY folded LIMIT 4097"); std::vector<FileCollection> values;
    while (query.row()) { if (values.size() == 4096) fail("collection listing exceeds limit"); values.push_back({query.text(0), query.text(1)}); } return values;
}
SavedFileFilter FileCatalog::saveFilter(const std::string &input, const FileFilter &original) {
    const auto name = normalized(input, 512, false, true); const auto filter = normalizeFilter(original); std::lock_guard lock(d->mutex); d->writable(); Transaction transaction(d->db);
    SavedFileFilter result{newKey(d->db, "filter-"), name, filter}; Statement existing(d->db, "SELECT key FROM saved_filters WHERE folded=?"); existing.bind(1, normalized(name, 512, true)); if (existing.row()) result.key = existing.text(0);
    else if (scalar(d->db, "SELECT count(*) FROM saved_filters") >= 4096) fail("saved filter count exceeds limit");
    Statement insert(d->db, "INSERT INTO saved_filters VALUES(?,?,?,?) ON CONFLICT(key) DO UPDATE SET name=excluded.name,folded=excluded.folded,filter=excluded.filter");
    insert.bind(1, result.key); insert.bind(2, name); insert.bind(3, normalized(name, 512, true)); insert.bind(4, filterJson(filter).dump()); insert.done(); d->bump(); transaction.commit(); return result;
}
std::optional<SavedFileFilter> FileCatalog::savedFilter(const std::string &key) const {
    std::lock_guard lock(d->mutex); d->intact(); Statement q(d->db, "SELECT name,filter FROM saved_filters WHERE key=?"); q.bind(1, normalized(key, 128, false, true)); if (!q.row()) return {}; return SavedFileFilter{key, q.text(0), parseFilter(q.text(1))};
}
std::vector<SavedFileFilter> FileCatalog::savedFilters() const {
    std::lock_guard lock(d->mutex); d->intact(); Statement q(d->db, "SELECT key,name,filter FROM saved_filters ORDER BY folded LIMIT 4097"); std::vector<SavedFileFilter> result;
    while (q.row()) { if (result.size() == 4096) fail("saved filter listing exceeds limit"); result.push_back({q.text(0), q.text(1), parseFilter(q.text(2))}); } return result;
}
void FileCatalog::removeFilter(const std::string &key) {
    std::lock_guard lock(d->mutex); d->writable(); Transaction transaction(d->db); Statement remove(d->db, "DELETE FROM saved_filters WHERE key=?"); remove.bind(1, normalized(key, 128, false, true)); remove.done(); if (!sqlite3_changes(d->db)) fail("saved filter does not exist"); d->bump(); transaction.commit();
}
FilePage FileCatalog::searchSaved(const std::string &key, const std::optional<FileCursor> &cursor, std::size_t limit) const {
    const auto saved = savedFilter(key); if (!saved) fail("saved filter does not exist"); return search(saved->filter, cursor, limit);
}
std::size_t FileCatalog::publishMetadata(std::size_t limit) {
    if (!limit || limit > maximumPage) fail("metadata publication batch exceeds limit"); std::lock_guard lock(d->mutex); d->writable();
    std::vector<std::string> keys; { Statement q(d->db, "SELECT e.key FROM pending p JOIN files e ON e.id=p.file_id ORDER BY p.file_id LIMIT ?"); q.bind(1, std::int64_t(limit)); while (q.row()) keys.push_back(q.text(0)); }
    std::size_t published = 0; for (const auto &key : keys) if (d->publish(key)) ++published; return published;
}
std::uint64_t FileCatalog::pendingMetadata() const { std::lock_guard lock(d->mutex); d->intact(); return std::uint64_t(scalar(d->db, "SELECT count(*) FROM pending")); }
std::string FileCatalog::metadataDocument(const std::string &key) const {
    std::lock_guard lock(d->mutex); d->intact(); Transaction transaction(d->db, false); const auto text = d->document(d->require(key)); transaction.commit(); return text;
}
std::string FileCatalog::presentationDocument(const std::string &key) const {
    std::lock_guard lock(d->mutex); d->intact(); const auto file = d->require(key);
#ifdef IIFILEPROVIDER_HAS_PRESENTATION
    if (const auto record = native::Presentation::load(d->path(file.path))) return record->document;
#endif
    return {};
}
std::string FileCatalog::classificationDocument(const fs::path &asset) {
    if (asset.empty() || !asset.is_absolute() || asset.lexically_normal() != asset
        || asset.native().find(fs::path::value_type{}) != asset.native().npos)
        fail("classification requires an absolute native asset path");
    // Inspect every ancestor, including Windows reparse points. Inspecting the
    // final leaf alone would permit a redirected directory to escape its owner.
    const auto validate = [&] {
        auto at = asset.root_path();
        for (const auto &part : asset.relative_path()) {
            at /= part;
            (void)inspect(at, at != asset);
        }
    };
    validate();
    const auto before = inspect(asset);
    const auto path = nativePath(pathText(asset) + companionSuffix);
    if (!fs::exists(fs::symlink_status(path))) return {};
    const auto stored = companion(path);
    Json collections = Json::array();
    for (const auto &item : stored.at("collections"))
        collections.push_back({{"key", normalized(item.at("key").get<std::string>(), 128, false, true)},
            {"name", normalized(item.at("name").get<std::string>(), 512, false, true)}});
    validate();
    if (inspect(asset).version != before.version) fail("asset changed during classification inspection");
    return Json{{"format", "iiFileProvider.Metadata"}, {"schema", 1}, {"fileKey", stored.at("fileKey")},
        {"revision", stored.at("revision")}, {"metadata", metadataJson(parseMetadata(stored.at("metadata")))},
        {"collections", collections}}.dump();
}
} // namespace iiFileProvider
