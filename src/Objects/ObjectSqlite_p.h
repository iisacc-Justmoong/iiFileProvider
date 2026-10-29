#pragma once
#include "ObjectStore.h"
#include <sqlite3.h>
#include <chrono>
#include <span>
#include <stdexcept>

namespace iiFileProvider::storage_detail {
[[noreturn]] inline void fail(const std::string &message) { throw std::runtime_error(message); }
inline void check(sqlite3 *db, int code) { if (code != SQLITE_OK) fail(sqlite3_errmsg(db)); }
inline void execute(sqlite3 *db, const char *sql) { check(db, sqlite3_exec(db,sql,nullptr,nullptr,nullptr)); }
struct Query {
    sqlite3 *db;
    sqlite3_stmt *stmt = nullptr;
    Query(sqlite3 *connection, const std::string &sql) : db(connection) {
        check(db,sqlite3_prepare_v2(db,sql.c_str(),-1,&stmt,nullptr));
    }
    ~Query() { sqlite3_finalize(stmt); }
    Query(const Query &) = delete;
    void bind(int i, const std::string &s) {
        if (s.size()>2*1024*1024) fail("object metadata field exceeds limit");
        check(db,sqlite3_bind_text(stmt,i,s.data(),static_cast<int>(s.size()),SQLITE_TRANSIENT));
    }
    void bind(int i, std::int64_t n) { check(db,sqlite3_bind_int64(stmt,i,n)); }
    void blob(int i, std::span<const std::uint8_t> bytes) {
        if (bytes.size()>ObjectStore::chunkBytes) fail("object chunk exceeds limit");
        check(db,sqlite3_bind_blob(stmt,i,bytes.data(),static_cast<int>(bytes.size()),SQLITE_TRANSIENT));
    }
    bool row() {
        const auto result=sqlite3_step(stmt);
        if(result==SQLITE_ROW)return true;
        if(result!=SQLITE_DONE)check(db,result);
        return false;
    }
    void done() {if(row())fail("unexpected object database result");}
    std::string text(int i) const {
        if(sqlite3_column_type(stmt,i)!=SQLITE_TEXT)fail("invalid object text field");
        return {reinterpret_cast<const char *>(sqlite3_column_text(stmt,i)),static_cast<std::size_t>(sqlite3_column_bytes(stmt,i))};
    }
    std::int64_t number(int i) const {
        if(sqlite3_column_type(stmt,i)!=SQLITE_INTEGER)fail("invalid object integer field");
        return sqlite3_column_int64(stmt,i);
    }
    std::span<const std::uint8_t> bytes(int i) const {
        if(sqlite3_column_type(stmt,i)!=SQLITE_BLOB)fail("invalid object chunk field");
        return {static_cast<const std::uint8_t *>(sqlite3_column_blob(stmt,i)),static_cast<std::size_t>(sqlite3_column_bytes(stmt,i))};
    }
};
struct Transaction {
    sqlite3 *db;
    bool committed=false;
    explicit Transaction(sqlite3 *connection, bool write=true):db(connection) {execute(db,write?"BEGIN IMMEDIATE":"BEGIN");}
    ~Transaction() {if(!committed)sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);}
    void commit() {execute(db,"COMMIT");committed=true;}
};
inline std::int64_t scalar(sqlite3 *db,const char *sql) {Query q(db,sql);if(!q.row())fail("missing object database scalar");return q.number(0);}
inline std::int64_t now() {return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
}
