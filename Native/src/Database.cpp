#include "iiFileProviderDatabase.h"
#include "iiFileProviderStorage.h"
#include <sqlite3.h>
#include <cmath>
#include <limits>
#include <utility>
#include <algorithm>
#include <array>
#include <chrono>
#include <thread>

namespace iiFileProvider::native::sql {
namespace {
auto invalid(std::string message,Code code=Code::invalid_input) { return std::unexpected(Error{code,0,std::move(message)}); }
auto failure(sqlite3* db,int result) {
    Code code=Code::failure;
    switch(result&255) {
        case SQLITE_BUSY: case SQLITE_LOCKED: code=Code::busy; break;
        case SQLITE_CONSTRAINT: code=Code::constraint; break;
        case SQLITE_READONLY: code=Code::read_only; break;
        case SQLITE_TOOBIG: case SQLITE_NOMEM: code=Code::limit_exceeded; break;
        case SQLITE_RANGE: code=Code::invalid_input; break;
    }
    const auto* message=db && (sqlite3_errcode(db)&255)==(result&255)?sqlite3_errmsg(db):sqlite3_errstr(result);
    return std::unexpected(Error{code,result,message});
}
}
struct Database::State final {
    sqlite3* handle=nullptr;
    std::uint64_t serial=0, transaction=0;
    ~State() { sqlite3_close_v2(handle); }
};
struct Statement::State final {
    std::shared_ptr<Database::State> database;
    sqlite3_stmt* statement=nullptr;
    enum class Position { initial,row,done,failed } position=Position::initial;
    ~State() { sqlite3_finalize(statement); }
};
Database::Database()=default;
Database::~Database()=default;
Database::Database(Database&&) noexcept=default;
Database& Database::operator=(Database&&) noexcept=default;
Result<Database> Database::open(const std::filesystem::path& path,Options options) {
    const auto utf8=path.u8string();
    if (utf8.empty() || utf8.find(u8'\0')!=std::u8string::npos || utf8==u8":memory:")
        return invalid("A nonempty database file path is required");
    if (options.busy_timeout_ms<0 || options.maximum_value_bytes<1024) return invalid("Invalid database limits");
    const auto flags=options.mode==OpenMode::read_only?SQLITE_OPEN_READONLY:
        options.mode==OpenMode::read_write?SQLITE_OPEN_READWRITE:
        options.mode==OpenMode::create?SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE:0;
    if (!flags) return invalid("Unknown database mode");
    Database result; result.state_=std::make_shared<State>();
    auto rc=native::openDatabase(reinterpret_cast<const char*>(utf8.c_str()),&result.state_->handle,flags|SQLITE_OPEN_FULLMUTEX);
    if (rc!=SQLITE_OK) return failure(result.state_->handle,rc);
    sqlite3_extended_result_codes(result.state_->handle,1);
    sqlite3_busy_timeout(result.state_->handle,options.busy_timeout_ms);
    sqlite3_limit(result.state_->handle,SQLITE_LIMIT_LENGTH,options.maximum_value_bytes);
    return result;
}
Result<Database> Database::memory(Options options) {
    if (options.busy_timeout_ms<0 || options.maximum_value_bytes<1024) return invalid("Invalid database limits");
    Database result; result.state_=std::make_shared<State>();
    auto rc=native::openDatabase(":memory:",&result.state_->handle,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX);
    if (rc!=SQLITE_OK) return failure(result.state_->handle,rc);
    sqlite3_extended_result_codes(result.state_->handle,1);
    sqlite3_busy_timeout(result.state_->handle,options.busy_timeout_ms);
    sqlite3_limit(result.state_->handle,SQLITE_LIMIT_LENGTH,options.maximum_value_bytes);
    return result;
}
Result<Statement> Database::prepare(std::string_view sql) const {
    if (!state_) return invalid("Database is closed",Code::closed);
    if (sql.empty() || sql.size()>std::numeric_limits<int>::max() || sql.find('\0')!=sql.npos)
        return invalid("Expected one SQL statement without NUL");
    auto query=std::make_unique<Statement::State>(); query->database=state_;
    const std::string owned(sql);
    const char* tail=nullptr;
    auto rc=sqlite3_prepare_v2(state_->handle,owned.c_str(),static_cast<int>(owned.size()),&query->statement,&tail);
    if (rc!=SQLITE_OK) return failure(state_->handle,rc);
    if (!query->statement) return invalid("SQL contains no statement");
    sqlite3_stmt* extra=nullptr;
    const auto remaining=owned.c_str()+owned.size()-tail;
    rc=sqlite3_prepare_v2(state_->handle,tail,static_cast<int>(remaining),&extra,nullptr);
    const bool multiple=extra!=nullptr; sqlite3_finalize(extra);
    if (rc!=SQLITE_OK || multiple) return invalid("Only one SQL statement is admitted");
    return Statement(std::move(query));
}
Result<void> Database::execute(std::string_view sql) const {
    auto query=prepare(sql); if (!query) return std::unexpected(query.error());
    while(true) { auto next=query->step(); if(!next) return std::unexpected(next.error()); if(*next==Step::done) return {}; }
}
bool Database::in_transaction() const noexcept { return state_ && !sqlite3_get_autocommit(state_->handle); }
std::int64_t Database::changes() const noexcept { return state_?sqlite3_changes(state_->handle):0; }
void Database::interrupt() const noexcept { if(state_) sqlite3_interrupt(state_->handle); }
Result<void> Database::configure_durable() const {
    if(!state_) return invalid("Database is closed",Code::closed);
    auto rc=sqlite3_db_config(state_->handle,SQLITE_DBCONFIG_DEFENSIVE,1,nullptr);
    if(rc!=SQLITE_OK) return failure(state_->handle,rc);
    for(auto sql:{"PRAGMA trusted_schema=OFF","PRAGMA journal_mode=DELETE","PRAGMA synchronous=EXTRA","PRAGMA fullfsync=ON"})
        if(auto result=execute(sql);!result) return result;
    auto journal=prepare("PRAGMA journal_mode"); if(!journal) return std::unexpected(journal.error());
    auto next=journal->step(); if(!next) return std::unexpected(next.error());
    auto mode=journal->value(0); if(!mode) return std::unexpected(mode.error());
    if(!std::holds_alternative<std::string>(*mode) || std::get<std::string>(*mode)!="delete")
        return invalid("Durable authoring requires DELETE journaling",Code::failure);
    return {};
}
Result<bool> Database::has_moved() const {
    if(!state_) return invalid("Database is closed",Code::closed);
    int moved=0; const auto rc=sqlite3_file_control(state_->handle,"main",SQLITE_FCNTL_HAS_MOVED,&moved);
    if(rc!=SQLITE_OK && rc!=SQLITE_NOTFOUND) return failure(state_->handle,rc);
    return moved!=0;
}
Result<std::uint64_t> Database::patch_blob(std::string_view table,std::string_view column,
        std::int64_t row_id,std::span<const std::uint8_t> replacement) const {
    if(!state_) return invalid("Database is closed",Code::closed);
    if(!in_transaction()) return invalid("BLOB patches require an explicit transaction");
    if(table.empty() || column.empty() || table.find('\0')!=table.npos || column.find('\0')!=column.npos
            || replacement.size()>std::size_t(std::numeric_limits<int>::max())) return invalid("Invalid BLOB patch");
    sqlite3_blob* raw=nullptr; const std::string owned_table(table),owned_column(column);
    auto rc=sqlite3_blob_open(state_->handle,"main",owned_table.c_str(),owned_column.c_str(),row_id,1,&raw);
    if(rc!=SQLITE_OK) return failure(state_->handle,rc);
    std::unique_ptr<sqlite3_blob,decltype(&sqlite3_blob_close)> blob(raw,sqlite3_blob_close);
    if(replacement.size()!=std::size_t(sqlite3_blob_bytes(raw))) return invalid("BLOB patch must preserve storage size");
    std::array<std::uint8_t,4096> previous{}; std::uint64_t written=0;
    for(std::size_t offset=0;offset<replacement.size();offset+=previous.size()) {
        const auto count=std::min(previous.size(),replacement.size()-offset);
        rc=sqlite3_blob_read(raw,previous.data(),int(count),int(offset)); if(rc!=SQLITE_OK) return failure(state_->handle,rc);
        std::size_t i=0;
        while(i<count) {
            if(previous[i]==replacement[offset+i]) {++i;continue;}
            const auto first=i; while(i<count && previous[i]!=replacement[offset+i]) ++i;
            rc=sqlite3_blob_write(raw,replacement.data()+offset+first,int(i-first),int(offset+first));
            if(rc!=SQLITE_OK) return failure(state_->handle,rc); written+=i-first;
        }
    }
    rc=sqlite3_blob_close(blob.release()); if(rc!=SQLITE_OK) return failure(state_->handle,rc);
    return written;
}
Result<void> Database::backup_to(Database& destination,std::uint64_t maximum_bytes,int timeout_ms) const {
    if(!state_ || !destination.state_) return invalid("Database is closed",Code::closed);
    if(state_==destination.state_ || destination.in_transaction() || timeout_ms<=0 || maximum_bytes==0)
        return invalid("Invalid snapshot destination or bounds");
    auto scalar=[&](std::string_view sql)->Result<std::int64_t> {
        auto query=prepare(sql); if(!query) return std::unexpected(query.error());
        auto step=query->step(); if(!step) return std::unexpected(step.error());
        auto value=query->value(0); if(!value) return std::unexpected(value.error());
        if(!std::holds_alternative<std::int64_t>(*value)) return invalid("Invalid database size metadata",Code::failure);
        return std::get<std::int64_t>(*value);
    };
    const auto size=scalar("PRAGMA page_size"),pages=scalar("PRAGMA page_count");
    if(!size) return std::unexpected(size.error()); if(!pages) return std::unexpected(pages.error());
    if(*size<=0 || *pages<0 || std::uint64_t(*size)>maximum_bytes || std::uint64_t(*pages)>maximum_bytes/std::uint64_t(*size))
        return invalid("Database snapshot exceeds byte limit",Code::limit_exceeded);
    auto* raw=sqlite3_backup_init(destination.state_->handle,"main",state_->handle,"main");
    if(!raw) return failure(destination.state_->handle,sqlite3_errcode(destination.state_->handle));
    std::unique_ptr<sqlite3_backup,decltype(&sqlite3_backup_finish)> backup(raw,sqlite3_backup_finish);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout_ms);
    while(true) {
        if(std::chrono::steady_clock::now()>=deadline) return invalid("Database snapshot timed out",Code::busy);
        const auto rc=sqlite3_backup_step(raw,128),count=sqlite3_backup_pagecount(raw);
        if(count<0 || std::uint64_t(count)>maximum_bytes/std::uint64_t(*size))
            return invalid("Database snapshot grew beyond byte limit",Code::limit_exceeded);
        if(rc==SQLITE_DONE) break;
        if(rc==SQLITE_BUSY || rc==SQLITE_LOCKED) {std::this_thread::sleep_for(std::chrono::milliseconds(1));continue;}
        if(rc!=SQLITE_OK) return failure(destination.state_->handle,rc);
    }
    const auto rc=sqlite3_backup_finish(backup.release()); if(rc!=SQLITE_OK) return failure(destination.state_->handle,rc);
    return {};
}
Statement::Statement(std::unique_ptr<State> state):state_(std::move(state)){}
Statement::~Statement()=default;
Statement::Statement(Statement&&) noexcept=default;
Statement& Statement::operator=(Statement&&) noexcept=default;
Result<void> Statement::bind(int index,const Value& value) {
    if (!state_) return invalid("Statement is closed",Code::closed);
    if (state_->position!=State::Position::initial) return invalid("Reset the statement before binding");
    auto* db=state_->database->handle; auto* stmt=state_->statement;
    const auto maximum=sqlite3_limit(db,SQLITE_LIMIT_LENGTH,-1);
    auto rc=std::visit([&](const auto& x)->int {
        using T=std::decay_t<decltype(x)>;
        if constexpr(std::is_same_v<T,std::monostate>) return sqlite3_bind_null(stmt,index);
        else if constexpr(std::is_same_v<T,std::int64_t>) return sqlite3_bind_int64(stmt,index,x);
        else if constexpr(std::is_same_v<T,double>) return std::isfinite(x)?sqlite3_bind_double(stmt,index,x):SQLITE_RANGE;
        else {
            if(x.size()>static_cast<std::size_t>(maximum)) return SQLITE_TOOBIG;
            if constexpr(std::is_same_v<T,std::string>) return sqlite3_bind_text(stmt,index,x.data(),static_cast<int>(x.size()),SQLITE_TRANSIENT);
            else return x.empty()?sqlite3_bind_zeroblob(stmt,index,0):sqlite3_bind_blob(stmt,index,x.data(),static_cast<int>(x.size()),SQLITE_TRANSIENT);
        }
    },value);
    if(rc!=SQLITE_OK) return failure(db,rc); return {};
}
Result<Step> Statement::step() {
    if(!state_) return invalid("Statement is closed",Code::closed);
    if(state_->position==State::Position::done) return Step::done;
    if(state_->position==State::Position::failed) return invalid("Reset the failed statement before stepping");
    auto* db=state_->database->handle; auto rc=sqlite3_step(state_->statement);
    if(sqlite3_get_autocommit(db)) state_->database->transaction=0;
    if(rc==SQLITE_ROW) { state_->position=State::Position::row; return Step::row; }
    if(rc==SQLITE_DONE) { state_->position=State::Position::done; return Step::done; }
    state_->position=State::Position::failed; return failure(db,rc);
}
Result<void> Statement::reset(bool clear) {
    if(!state_) return invalid("Statement is closed",Code::closed);
    // reset() returns the previous step error, even though reset succeeded.
    sqlite3_reset(state_->statement); state_->position=State::Position::initial;
    if(clear) { auto rc=sqlite3_clear_bindings(state_->statement); if(rc!=SQLITE_OK) return failure(state_->database->handle,rc); }
    return {};
}
int Statement::columns() const noexcept { return state_?sqlite3_column_count(state_->statement):0; }
Result<std::string> Statement::column_name(int column) const {
    if(column<0 || column>=columns()) return invalid("Column index is out of range");
    const auto* name=sqlite3_column_name(state_->statement,column);
    if(!name) return failure(state_->database->handle,SQLITE_NOMEM); return std::string(name);
}
Result<Value> Statement::value(int column) const {
    if(!state_ || state_->position!=State::Position::row || column<0 || column>=columns()) return invalid("No current value at this column");
    auto* q=state_->statement;
    switch(sqlite3_column_type(q,column)) {
        case SQLITE_NULL: return Value{};
        case SQLITE_INTEGER: return Value{std::int64_t(sqlite3_column_int64(q,column))};
        case SQLITE_FLOAT: return Value{sqlite3_column_double(q,column)};
        case SQLITE_TEXT: {
            const auto* p=sqlite3_column_text(q,column); auto n=sqlite3_column_bytes(q,column);
            if(!p && n) return failure(state_->database->handle,SQLITE_NOMEM);
            return Value{std::string(p?reinterpret_cast<const char*>(p):"",n)};
        }
        case SQLITE_BLOB: {
            const auto* p=static_cast<const std::uint8_t*>(sqlite3_column_blob(q,column)); auto n=sqlite3_column_bytes(q,column);
            if(!p && n) return failure(state_->database->handle,SQLITE_NOMEM);
            return Value{n?Blob(p,p+n):Blob{}};
        }
    }
    return invalid("Unknown SQLite column type");
}
Transaction::Transaction(std::shared_ptr<Database::State> state,std::uint64_t id):state_(std::move(state)),id_(id){}
Transaction::~Transaction() { if(state_ && state_->transaction==id_) (void)rollback(); }
Transaction::Transaction(Transaction&& other) noexcept:state_(std::move(other.state_)),id_(other.id_){}
Transaction& Transaction::operator=(Transaction&& other) noexcept {
    if(this!=&other) { if(state_ && state_->transaction==id_) (void)rollback(); state_=std::move(other.state_); id_=other.id_; } return *this;
}
Result<Transaction> Transaction::begin(Database& database,bool writing) {
    if(!database.state_) return invalid("Database is closed",Code::closed);
    auto result=database.execute(writing?"BEGIN IMMEDIATE":"BEGIN"); if(!result) return std::unexpected(result.error());
    auto& state=*database.state_; state.transaction=++state.serial;
    return Transaction(database.state_,state.transaction);
}
Result<void> Transaction::commit() {
    if(!state_ || state_->transaction!=id_) return invalid("Transaction is no longer active");
    auto rc=sqlite3_exec(state_->handle,"COMMIT",nullptr,nullptr,nullptr);
    if(rc!=SQLITE_OK) return failure(state_->handle,rc); state_->transaction=0; state_.reset(); return {};
}
Result<void> Transaction::rollback() {
    if(!state_ || state_->transaction!=id_) return invalid("Transaction is no longer active");
    auto rc=sqlite3_exec(state_->handle,"ROLLBACK",nullptr,nullptr,nullptr);
    if(rc!=SQLITE_OK) return failure(state_->handle,rc); state_->transaction=0; state_.reset(); return {};
}
}
