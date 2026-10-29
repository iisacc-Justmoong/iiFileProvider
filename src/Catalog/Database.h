#pragma once
#include "Export.h"
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace iiFileProvider {
class Statement;
class Transaction;

// SQLite is private to this implementation. Schemas and record meaning remain
// in the caller; all connection, statement, transaction and BLOB I/O lives here.
class IIFILEPROVIDER_EXPORT Database final {
public:
    explicit Database(const std::string &utf8Path, bool readOnly = false);
    ~Database();
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;
    void execute(const char *sql);
    std::int64_t scalar(const char *sql);
    void configureDurable();
    bool hasMoved() const;
    bool inTransaction() const noexcept;
    std::uint64_t patchBlob(const char *table, const char *column,
        std::int64_t rowId, std::span<const std::uint8_t> replacement);
    // Copy the current consistent snapshot, including committed WAL content.
    void backupTo(Database &destination, std::uint64_t maximumBytes, int timeoutMs = 30000);
private:
    friend class Statement;
    friend class Transaction;
    class Impl;
    std::shared_ptr<Impl> m_impl;
};

class IIFILEPROVIDER_EXPORT Statement final {
public:
    Statement(Database *database, const char *sql);
    ~Statement();
    Statement(const Statement &) = delete;
    Statement &operator=(const Statement &) = delete;
    void integer(int index, std::int64_t value);
    void text(int index, const std::string &value);
    void bytes(int index, const void *data, std::size_t size);
    bool row();
    void done();
    std::int64_t integer(int column) const;
    std::string text(int column) const;
    // The span remains valid until the next step or statement destruction.
    std::span<const std::uint8_t> bytes(int column) const;
private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

class IIFILEPROVIDER_EXPORT Transaction final {
public:
    Transaction(Database *database, bool writing);
    ~Transaction();
    Transaction(const Transaction &) = delete;
    Transaction &operator=(const Transaction &) = delete;
    void commit();
private:
    std::shared_ptr<Database::Impl> m_database;
    bool m_committed = false;
};
} // namespace iiFileProvider
