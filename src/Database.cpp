#include "Database.h"
#include "File.h"
#include <sqlite3.h>
#include <algorithm>
#include <array>
#include <QElapsedTimer>
#include <limits>

namespace iiFileProvider {
namespace {
void check(int result, sqlite3 *database) {
    if (result != SQLITE_OK)
        throw FileError(FileCode::IoError, database ? sqlite3_errmsg(database) : sqlite3_errstr(result));
}
void executeSql(sqlite3 *database, const char *sql) {
    check(sqlite3_exec(database, sql, nullptr, nullptr, nullptr), database);
}
}
class Database::Impl {
public:
    sqlite3 *handle = nullptr;
    ~Impl() { sqlite3_close_v2(handle); }
};
Database::Database(const std::string &path, bool readOnly) : m_impl(std::make_shared<Impl>()) {
    const auto target = File::absolutePath(File::pathString(std::filesystem::u8path(path)));
    check(sqlite3_open_v2(target.toUtf8().constData(), &m_impl->handle,
        (readOnly ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE) | SQLITE_OPEN_FULLMUTEX,
        nullptr), m_impl->handle);
    sqlite3_busy_timeout(m_impl->handle, 0);
}
Database::~Database() = default;
void Database::execute(const char *sql) { executeSql(m_impl->handle, sql); }
std::int64_t Database::scalar(const char *sql) {
    Statement query(this, sql);
    if (!query.row()) throw FileError(FileCode::CorruptFile, "required database field is missing");
    const auto value = query.integer(0);
    if (query.row()) throw FileError(FileCode::CorruptFile, "duplicate scalar field");
    return value;
}
void Database::configureDurable() {
    check(sqlite3_db_config(m_impl->handle, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr), m_impl->handle);
    execute("PRAGMA trusted_schema=OFF");
    Statement journal(this, "PRAGMA journal_mode=DELETE");
    if (!journal.row() || journal.text(0) != "delete")
        throw FileError(FileCode::IoError, "DELETE journaling is required for direct file writes");
    journal.done();
    execute("PRAGMA synchronous=EXTRA");
    execute("PRAGMA fullfsync=ON");
    if (scalar("PRAGMA synchronous") != 3)
        throw FileError(FileCode::IoError, "durable synchronization is unavailable");
}
bool Database::hasMoved() const {
    int moved = 0;
    const int result = sqlite3_file_control(m_impl->handle, "main", SQLITE_FCNTL_HAS_MOVED, &moved);
    if (result != SQLITE_OK && result != SQLITE_NOTFOUND) check(result, m_impl->handle);
    return moved != 0;
}
bool Database::inTransaction() const noexcept { return !sqlite3_get_autocommit(m_impl->handle); }
void Database::backupTo(Database &destination, std::uint64_t maximumBytes, int timeoutMs) {
    const auto pageSize = scalar("PRAGMA page_size");
    const auto pages = scalar("PRAGMA page_count");
    if (pageSize <= 0 || pages < 0 || std::uint64_t(pageSize) > maximumBytes
        || std::uint64_t(pages) > maximumBytes / std::uint64_t(pageSize))
        throw FileError(FileCode::LimitExceeded, "database snapshot exceeds byte limit");
    sqlite3_backup *raw = sqlite3_backup_init(destination.m_impl->handle, "main", m_impl->handle, "main");
    if (!raw) throw FileError(FileCode::IoError, sqlite3_errmsg(destination.m_impl->handle));
    std::unique_ptr<sqlite3_backup, decltype(&sqlite3_backup_finish)> backup(raw, sqlite3_backup_finish);
    QElapsedTimer timer; timer.start();
    while (true) {
        if (timer.elapsed() >= timeoutMs) throw FileError(FileCode::TimedOut, "database backup timed out");
        const auto status = sqlite3_backup_step(raw, 128);
        const auto currentPages = sqlite3_backup_pagecount(raw);
        if (currentPages < 0 || std::uint64_t(currentPages) > maximumBytes / std::uint64_t(pageSize))
            throw FileError(FileCode::LimitExceeded, "database backup grew beyond byte limit");
        if (status == SQLITE_DONE) break;
        check(status, destination.m_impl->handle);
    }
    check(sqlite3_backup_finish(backup.release()), destination.m_impl->handle);
}
std::uint64_t Database::patchBlob(const char *table, const char *column, std::int64_t rowId,
                                 std::span<const std::uint8_t> replacement) {
    if (!inTransaction()) throw FileError(FileCode::Conflict, "BLOB patches require an explicit transaction");
    sqlite3_blob *raw = nullptr;
    check(sqlite3_blob_open(m_impl->handle, "main", table, column, rowId, 1, &raw), m_impl->handle);
    std::unique_ptr<sqlite3_blob, decltype(&sqlite3_blob_close)> blob(raw, sqlite3_blob_close);
    if (replacement.size() != static_cast<std::size_t>(sqlite3_blob_bytes(raw)))
        throw FileError(FileCode::LimitExceeded, "BLOB patch size must match existing storage");
    std::array<std::uint8_t, 4096> previous{};
    std::uint64_t written = 0;
    for (std::size_t offset = 0; offset < replacement.size(); offset += previous.size()) {
        const auto count = std::min(previous.size(), replacement.size() - offset);
        check(sqlite3_blob_read(raw, previous.data(), static_cast<int>(count), static_cast<int>(offset)), m_impl->handle);
        std::size_t index = 0;
        while (index < count) {
            if (previous[index] == replacement[offset + index]) { ++index; continue; }
            const auto first = index;
            auto last = index + 1;
            while (++index < count && index - last < 32) {
                if (previous[index] != replacement[offset + index]) last = index + 1;
            }
            check(sqlite3_blob_write(raw, replacement.data() + offset + first,
                static_cast<int>(last - first), static_cast<int>(offset + first)), m_impl->handle);
            written += last - first;
        }
    }
    check(sqlite3_blob_close(blob.release()), m_impl->handle);
    return written;
}
class Statement::Impl {
public:
    std::shared_ptr<Database::Impl> database;
    sqlite3_stmt *statement = nullptr;
    ~Impl() { sqlite3_finalize(statement); }
    void requireType(int column, int type) const {
        if (sqlite3_column_type(statement, column) != type)
            throw FileError(FileCode::CorruptFile, "database field type is invalid");
    }
};
Statement::Statement(Database *database, const char *sql) : m_impl(std::make_unique<Impl>()) {
    if (!database) throw FileError(FileCode::InvalidPath, "database is required");
    m_impl->database = database->m_impl;
    check(sqlite3_prepare_v2(m_impl->database->handle, sql, -1, &m_impl->statement, nullptr), m_impl->database->handle);
}
Statement::~Statement() = default;
void Statement::integer(int index, std::int64_t value) {
    check(sqlite3_bind_int64(m_impl->statement, index, value), m_impl->database->handle);
}
void Statement::text(int index, const std::string &value) {
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw FileError(FileCode::LimitExceeded, "text exceeds SQLite limit");
    check(sqlite3_bind_text(m_impl->statement, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT), m_impl->database->handle);
}
void Statement::bytes(int index, const void *data, std::size_t size) {
    if (size > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw FileError(FileCode::LimitExceeded, "record exceeds SQLite BLOB limit");
    check(size == 0 ? sqlite3_bind_zeroblob(m_impl->statement, index, 0)
        : sqlite3_bind_blob(m_impl->statement, index, data, static_cast<int>(size), SQLITE_TRANSIENT), m_impl->database->handle);
}
bool Statement::row() {
    const auto result = sqlite3_step(m_impl->statement);
    if (result == SQLITE_ROW) return true;
    if (result != SQLITE_DONE) check(result, m_impl->database->handle);
    return false;
}
void Statement::done() {
    if (row()) throw FileError(FileCode::CorruptFile, "unexpected query result");
}
std::int64_t Statement::integer(int column) const {
    m_impl->requireType(column, SQLITE_INTEGER); return sqlite3_column_int64(m_impl->statement, column);
}
std::string Statement::text(int column) const {
    m_impl->requireType(column, SQLITE_TEXT);
    return {reinterpret_cast<const char *>(sqlite3_column_text(m_impl->statement, column)),
        static_cast<std::size_t>(sqlite3_column_bytes(m_impl->statement, column))};
}
std::span<const std::uint8_t> Statement::bytes(int column) const {
    m_impl->requireType(column, SQLITE_BLOB);
    return {static_cast<const std::uint8_t *>(sqlite3_column_blob(m_impl->statement, column)),
        static_cast<std::size_t>(sqlite3_column_bytes(m_impl->statement, column))};
}
Transaction::Transaction(Database *database, bool writing) {
    if (!database) throw FileError(FileCode::InvalidPath, "database is required");
    m_database = database->m_impl;
    executeSql(m_database->handle, writing ? "BEGIN IMMEDIATE" : "BEGIN");
}
Transaction::~Transaction() {
    if (!m_committed) sqlite3_exec(m_database->handle, "ROLLBACK", nullptr, nullptr, nullptr);
}
void Transaction::commit() { executeSql(m_database->handle, "COMMIT"); m_committed = true; }
} // namespace iiFileProvider
