#pragma once
#include "iiFileProviderNativeExport.h"
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace iiFileProvider::native::sql {
enum class Code { invalid_input, closed, busy, constraint, read_only, limit_exceeded, failure };
struct Error final { Code code; int native_code; std::string message; };
template<class T> using Result = std::expected<T, Error>;
using Blob = std::vector<std::uint8_t>;
using Value = std::variant<std::monostate, std::int64_t, double, std::string, Blob>;
enum class OpenMode { read_only, read_write, create };
struct Options final {
    OpenMode mode = OpenMode::read_write;
    int busy_timeout_ms = 0;
    int maximum_value_bytes = 16 * 1024 * 1024;
};
enum class Step { row, done };
class Statement;
class Transaction;
class IIFILEPROVIDER_NATIVE_EXPORT Database final {
public:
    Database();
    ~Database();
    Database(Database&&) noexcept;
    Database& operator=(Database&&) noexcept;
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    [[nodiscard]] static Result<Database> open(const std::filesystem::path&, Options = {});
    [[nodiscard]] static Result<Database> memory(Options = {});
    [[nodiscard]] Result<Statement> prepare(std::string_view) const;
    [[nodiscard]] Result<void> execute(std::string_view) const;
    [[nodiscard]] bool in_transaction() const noexcept;
    [[nodiscard]] std::int64_t changes() const noexcept;
    // Direct authoring files use DELETE journaling and EXTRA synchronization.
    [[nodiscard]] Result<void> configure_durable() const;
    [[nodiscard]] Result<bool> has_moved() const;
    // Same-size incremental BLOB writes require an explicit writing transaction.
    // Returns the number of changed bytes actually written; rollback restores them.
    [[nodiscard]] Result<std::uint64_t> patch_blob(std::string_view table, std::string_view column,
        std::int64_t row_id, std::span<const std::uint8_t>) const;
    // Consistent SQLite snapshot, including committed WAL content, bounded by size/time.
    [[nodiscard]] Result<void> backup_to(Database& destination, std::uint64_t maximum_bytes,
        int timeout_ms=30000) const;
    // May be called from another thread while the Database object remains alive.
    void interrupt() const noexcept;
private:
    struct State;
    std::shared_ptr<State> state_;
    friend class Statement;
    friend class Transaction;
};
class IIFILEPROVIDER_NATIVE_EXPORT Statement final {
public:
    ~Statement();
    Statement(Statement&&) noexcept;
    Statement& operator=(Statement&&) noexcept;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    [[nodiscard]] Result<void> bind(int index, const Value&);
    [[nodiscard]] Result<Step> step();
    [[nodiscard]] Result<void> reset(bool clear_bindings = true);
    [[nodiscard]] int columns() const noexcept;
    [[nodiscard]] Result<std::string> column_name(int column) const;
    [[nodiscard]] Result<Value> value(int column) const;
private:
    struct State;
    std::unique_ptr<State> state_;
    explicit Statement(std::unique_ptr<State>);
    friend class Database;
};
class IIFILEPROVIDER_NATIVE_EXPORT Transaction final {
public:
    ~Transaction();
    Transaction(Transaction&&) noexcept;
    Transaction& operator=(Transaction&&) noexcept;
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    [[nodiscard]] static Result<Transaction> begin(Database&, bool writing = true);
    [[nodiscard]] Result<void> commit();
    [[nodiscard]] Result<void> rollback();
private:
    std::shared_ptr<Database::State> state_;
    std::uint64_t id_=0;
    explicit Transaction(std::shared_ptr<Database::State>,std::uint64_t);
};
}
