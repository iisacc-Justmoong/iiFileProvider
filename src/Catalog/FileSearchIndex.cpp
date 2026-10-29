#include "FileSearchIndex.h"

#include <sqlite3.h>
#include <algorithm>
#include <cctype>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace iiFileProvider {
namespace {
void check(sqlite3* db, int code) {
    if (code != SQLITE_OK && code != SQLITE_ROW && code != SQLITE_DONE)
        throw std::runtime_error(db ? sqlite3_errmsg(db) : "SQLite error");
}
void exec(sqlite3* db, const char* sql) { check(db, sqlite3_exec(db, sql, nullptr, nullptr, nullptr)); }
class Statement {
public:
    Statement(sqlite3* db, const char* sql) : db_(db) { check(db_, sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr)); }
    ~Statement() { sqlite3_finalize(stmt_); }
    void bind(int i, const std::string& value) {
        if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) throw std::invalid_argument("Search field exceeds SQLite limit");
        check(db_, sqlite3_bind_text(stmt_, i, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT));
    }
    void bind(int i, std::int64_t value) { check(db_, sqlite3_bind_int64(stmt_, i, value)); }
    bool row() { const int code=sqlite3_step(stmt_); check(db_, code); return code==SQLITE_ROW; }
    std::string string(int i) const {
        const auto* value=sqlite3_column_text(stmt_,i);
        return value ? std::string(reinterpret_cast<const char*>(value), sqlite3_column_bytes(stmt_,i)) : std::string{};
    }
    std::int64_t number(int i) const { return sqlite3_column_int64(stmt_,i); }
private:
    sqlite3* db_;
    sqlite3_stmt* stmt_=nullptr;
};
std::string matchExpression(std::string_view input) {
    if (input.size()>4096 || input.find('\0')!=std::string_view::npos)
        throw std::invalid_argument("Search text exceeds its limit or contains NUL");
    std::string result;
    for (std::size_t at=0; at<input.size();) {
        while (at<input.size() && std::isspace(static_cast<unsigned char>(input[at]))) ++at;
        if (at==input.size()) break;
        std::string token;
        while (at<input.size() && !std::isspace(static_cast<unsigned char>(input[at]))) {
            if (input[at]=='"') token+='"';
            token+=input[at++];
        }
        if (!result.empty()) result+=" AND ";
        result+='"'+token+"\"*";
    }
    return result;
}
}

class FileSearchIndex::Impl {
public:
    Impl() {
        const int code=sqlite3_open_v2(":memory:", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
        if(code!=SQLITE_OK) {
            std::string message=db ? sqlite3_errmsg(db) : "Cannot open in-memory SQLite index";
            if(db) sqlite3_close(db);
            db=nullptr;
            throw std::runtime_error(message);
        }
        try { exec(db, R"SQL(
CREATE TABLE entries(id INTEGER PRIMARY KEY, path TEXT UNIQUE NOT NULL, title TEXT NOT NULL,
 file_key TEXT NOT NULL, mime TEXT NOT NULL, classification TEXT NOT NULL, size INTEGER NOT NULL,
 classified INTEGER NOT NULL, presented INTEGER NOT NULL, pending INTEGER NOT NULL, terms TEXT NOT NULL);
CREATE VIRTUAL TABLE search_index USING fts5(path, terms, content='entries', content_rowid='id',
 tokenize='unicode61 remove_diacritics 2', prefix='2 3 4');
CREATE TRIGGER entries_insert AFTER INSERT ON entries BEGIN
 INSERT INTO search_index(rowid,path,terms) VALUES(new.id,new.path,new.terms); END;
CREATE TRIGGER entries_delete AFTER DELETE ON entries BEGIN
 INSERT INTO search_index(search_index,rowid,path,terms) VALUES('delete',old.id,old.path,old.terms); END;
CREATE TRIGGER entries_update AFTER UPDATE ON entries BEGIN
 INSERT INTO search_index(search_index,rowid,path,terms) VALUES('delete',old.id,old.path,old.terms);
 INSERT INTO search_index(rowid,path,terms) VALUES(new.id,new.path,new.terms); END;
)SQL"); } catch (...) { sqlite3_close(db); db=nullptr; throw; }
    }
    ~Impl() { if(db) sqlite3_close(db); }
    sqlite3* db=nullptr;
    std::unordered_set<std::string> indexed;
    mutable std::mutex mutex;
};

FileSearchIndex::FileSearchIndex() : d(std::make_unique<Impl>()) {}
FileSearchIndex::~FileSearchIndex() = default;
bool FileSearchIndex::upsert(const FileSearchEntry& e) {
    if(e.path.empty() || e.path.find('\0')!=std::string::npos) throw std::invalid_argument("Search path is empty or contains NUL");
    std::lock_guard lock(d->mutex);
    const auto [position, created] = d->indexed.insert(e.path);
    try {
    Statement insert(d->db,R"SQL(
INSERT INTO entries(path,title,file_key,mime,classification,size,classified,presented,pending,terms)
 VALUES(?,?,?,?,?,?,?,?,?,?)
 ON CONFLICT(path) DO UPDATE SET
 title=CASE WHEN entries.classified=1 THEN entries.title WHEN excluded.classified=1 THEN excluded.title ELSE entries.title END,
 file_key=CASE WHEN entries.classified=1 THEN entries.file_key ELSE excluded.file_key END,
 mime=CASE WHEN excluded.mime<>'' THEN excluded.mime ELSE entries.mime END,
 classification=CASE WHEN entries.classified=1 THEN entries.classification ELSE excluded.classification END,
 size=excluded.size, classified=max(entries.classified,excluded.classified),
 presented=max(entries.presented,excluded.presented),
 pending=CASE WHEN entries.classified=1 THEN entries.pending ELSE excluded.pending END,
 terms=entries.terms || ' ' || excluded.terms
)SQL");
    insert.bind(1,e.path); insert.bind(2,e.title); insert.bind(3,e.fileKey); insert.bind(4,e.mimeType);
    insert.bind(5,e.classification);
    insert.bind(6,static_cast<std::int64_t>(std::min(e.size,std::uint64_t(std::numeric_limits<std::int64_t>::max()))));
    insert.bind(7,std::int64_t(e.hasClassification)); insert.bind(8,std::int64_t(e.hasPresentation));
    insert.bind(9,std::int64_t(e.metadataPending)); insert.bind(10,e.searchTerms);
    (void)insert.row();
    } catch (...) {
        if (created) d->indexed.erase(position);
        throw;
    }
    return created;
}
bool FileSearchIndex::contains(std::string_view path) const {
    std::lock_guard lock(d->mutex);
    return d->indexed.contains(std::string(path));
}
std::vector<FileSearchEntry> FileSearchIndex::search(std::string_view query,std::size_t limit) const {
    if(!limit || limit>1024) throw std::invalid_argument("Search limit must be between 1 and 1024");
    const auto match=matchExpression(query);
    std::lock_guard lock(d->mutex);
    const char* all="SELECT path,title,file_key,mime,classification,size,classified,presented,pending FROM entries ORDER BY path LIMIT ?";
    const char* filtered="SELECT e.path,e.title,e.file_key,e.mime,e.classification,e.size,e.classified,e.presented,e.pending FROM search_index JOIN entries e ON e.id=search_index.rowid WHERE search_index MATCH ? ORDER BY rank,e.path LIMIT ?";
    Statement statement(d->db,match.empty()?all:filtered);
    int parameter=1; if(!match.empty()) statement.bind(parameter++,match);
    statement.bind(parameter,static_cast<std::int64_t>(limit));
    std::vector<FileSearchEntry> result;
    while(statement.row()) {
        FileSearchEntry e;
        e.path=statement.string(0); e.title=statement.string(1); e.fileKey=statement.string(2);
        e.mimeType=statement.string(3); e.classification=statement.string(4); e.size=static_cast<std::uint64_t>(statement.number(5));
        e.hasClassification=bool(statement.number(6)); e.hasPresentation=bool(statement.number(7)); e.metadataPending=bool(statement.number(8));
        result.push_back(std::move(e));
    }
    return result;
}
} // namespace iiFileProvider
