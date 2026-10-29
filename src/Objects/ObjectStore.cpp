#include "ObjectStore.h"
#include "ObjectSqlite_p.h"
#include "ObjectHash.h"
#include "ObjectBatchRead.h"
#include "ObjectMetadataCodec_p.h"
#include "OntologySchema_p.h"
#include "TemporaryLease.h"
#include <sqlite3.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <limits>
#include <mutex>
#include <span>
#include <stdexcept>
#include <utility>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#endif

namespace iiFileProvider {
namespace {
namespace fs = std::filesystem;
using detail::ObjectHash;
constexpr int applicationId = 0x534F424A; // SOBJ
constexpr const char *columns = "r.object_key,o.index_key,r.version,r.path,r.sha256,r.size,"
    "r.author_origin,r.author_subject,r.author_name,r.session_key,r.at_ns,r.operation,r.deleted,r.parent_key,r.validation_key,"
    "r.metadata_format,r.metadata,r.source_stamp,r.record_format,s.profile_format,s.profile";
constexpr const char *join = " FROM revisions r JOIN objects o ON o.object_key=r.object_key JOIN sessions s ON s.session_key=r.session_key ";
[[noreturn]] void fail(const std::string &message) { throw std::runtime_error(message); }
using storage_detail::Query;
using storage_detail::Transaction;
using storage_detail::check;
using storage_detail::execute;
using storage_detail::scalar;
using storage_detail::now;
std::string uniqueKey(sqlite3 *db,const char *prefix) {Query q(db,"SELECT lower(hex(randomblob(16)))");if(!q.row())fail("object key unavailable");return std::string(prefix)+q.text(0);}
void field(const std::string &s,std::size_t max,bool required=true) {
    if((required && s.empty()) || s.size()>max || s.find('\0')!=std::string::npos)fail("invalid object metadata");
}
void metadataValid(const ObjectMetadata &document) {
    field(document.format,128,false);field(document.payload,2*1024*1024,false);
    if(document.format.empty()!=document.payload.empty())fail("incomplete object metadata document");
    if(!document.format.empty() && document.format!="iiFileProvider.FileAuthor/1" && document.format!="iiFileProvider.Authorship/3")
        fail("unsupported object metadata document format");
}
void authorValid(const ObjectAuthor &author) {
    field(author.origin,2048);field(author.subject,1024);field(author.displayName,4096,false);metadataValid(author.profile);
    if(!author.profile.format.empty() && author.profile.format!="iiFileProvider.FileAuthor/1")fail("actor profile requires FileAuthor metadata");
}
void authorshipValid(const ObjectMetadata &document) {
    metadataValid(document);
    if(!document.format.empty() && document.format!="iiFileProvider.Authorship/3")
        fail("original authorship requires an Authorship roster");
}
void validateDocument(sqlite3 *db,const ObjectMetadata &document) {
    metadataValid(document);if(document.payload.empty())return;
    // SQLite's JSON parser is private to the storage owner; no Qt parser leaks
    // into the C++23 target. FileAuthor/Authorship remain the domain producers.
    {Query q(db,"SELECT json_valid(?)");q.bind(1,document.payload);if(!q.row() || q.number(0)!=1)fail("invalid object metadata JSON");}
    const bool authorship=document.format=="iiFileProvider.Authorship/3";
    {Query q(db,"SELECT coalesce(json_type(?)='object' AND json_type(?,'$.schemaVersion')='integer' AND json_extract(?,'$.schemaVersion')=?,0)");
     q.bind(1,document.payload);q.bind(2,document.payload);q.bind(3,document.payload);q.bind(4,std::int64_t(authorship?3:1));
     if(!q.row() || q.number(0)!=1)fail("invalid object metadata schema");}
    const auto allowed=authorship?"'schemaVersion','revision','modifiedAt','authors','lastAuthor','firstEditor','participants','links'":"'schemaVersion','account','details','attribution','serviceOrigin','capturedAt','device'";
    {Query q(db,std::string("SELECT 1 FROM json_each(?) WHERE key NOT IN(")+allowed+") LIMIT 1");q.bind(1,document.payload);if(q.row())fail("unknown object metadata field");}
    {Query q(db,"SELECT 1 FROM json_tree(?) WHERE lower(key) IN('token','secret','authenticationtoken','accesstoken','refreshtoken','access_token','refresh_token','password','authorization') LIMIT 1");
     q.bind(1,document.payload);if(q.row())fail("runtime credentials are forbidden in object metadata");}
    {Query q(db,"SELECT 1 FROM json_tree(?) WHERE key IS NOT NULL GROUP BY parent,key HAVING count(*)>1 LIMIT 1");q.bind(1,document.payload);if(q.row())fail("duplicate object metadata key");}
    const auto shape=authorship?
        "json_type(?,'$.authors')='array' AND json_type(?,'$.participants')='array' AND json_type(?,'$.links')='array'":
        "json_type(?,'$.account')='object' AND json_type(?,'$.details')='object' AND json_type(?,'$.attribution')='object'";
    {Query q(db,std::string("SELECT coalesce(")+shape+",0)");for(int i=1;i<=3;++i)q.bind(i,document.payload);if(!q.row() || q.number(0)!=1)fail("incomplete object metadata document");}
    if(authorship) {
        Query q(db,"SELECT json_extract(value,'$.author') FROM json_each(?,'$.authors')");q.bind(1,document.payload);
        std::size_t count=0;while(q.row()) {
            if(++count>256)fail("too many object authors");
            validateDocument(db,{"iiFileProvider.FileAuthor/1",q.text(0)});
        }
    }
}
void validateActor(sqlite3 *db,const ObjectAuthor &author) {
    validateDocument(db,author.profile);
    if(author.profile.payload.empty())return;
    Query q(db,"SELECT json_extract(?,'$.account.sub'),json_extract(?,'$.serviceOrigin')");
    q.bind(1,author.profile.payload);q.bind(2,author.profile.payload);
    if(!q.row() || q.text(0)!=author.subject || q.text(1)!=author.origin)fail("object actor profile identity mismatch");
}
void pathValid(const std::string &path) {
    field(path,32768);
    if(path.front()=='/' || path.back()=='/' || path.find('\\')!=std::string::npos || path.find(':')!=std::string::npos)fail("logical object path must be relative");
    std::size_t offset=0;
    while(offset<path.size()) {
        const auto end=path.find('/',offset);const auto part=path.substr(offset,end==std::string::npos?end:end-offset);
        if(part.empty() || part=="." || part=="..")fail("logical object path contains traversal");
        if(end==std::string::npos)break;offset=end+1;
    }
}
fs::path directPath(const fs::path &input,bool existing) {
    const auto path=fs::absolute(input).lexically_normal();
    if(path.filename().empty())fail("object path has no filename");
    const auto canonical=existing?fs::canonical(path):fs::canonical(path.parent_path())/path.filename();
    if(canonical!=path || fs::is_symlink(fs::symlink_status(path)))fail("redirected object path is forbidden");
    return path;
}
FILE *exclusiveFile(const fs::path &path) {
#ifdef _WIN32
    int descriptor = -1;
    if (_wsopen_s(&descriptor, path.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
                  _SH_DENYRW, _S_IREAD | _S_IWRITE) != 0)
        return nullptr;
    FILE *file = _wfdopen(descriptor, L"wb");
    if (!file)
        _close(descriptor);
    return file;
#else
    return std::fopen(path.c_str(),"wbx");
#endif
}
void privatePermissions(const fs::path &path,bool directory=false) {
    fs::permissions(path,fs::perms::owner_read|fs::perms::owner_write|(directory?fs::perms::owner_exec:fs::perms::none),fs::perm_options::replace);
}
ObjectRecord record(Query &q) {
    ObjectRecord r;
    r.key=q.text(0);r.indexKey=q.number(1);r.version=q.number(2);r.path=q.text(3);r.sha256=q.text(4);r.size=q.number(5);
    r.author={q.text(6),q.text(7),q.text(8)};r.sessionKey=q.text(9);r.recordedAtNs=q.number(10);r.operation=q.text(11);
    r.deleted=q.number(12)!=0;r.parentValidationKey=q.text(13);r.validationKey=q.text(14);
    r.authorship={q.text(15),q.text(16)};r.sourceStamp=q.text(17);r.schemaVersion=static_cast<std::uint32_t>(q.number(18));
    r.author.profile={q.text(19),q.text(20)};return r;
}
std::string validationKey(const ObjectRecord &r,const ObjectSession &s,const std::string &container,
                          const std::string &context={},const std::string &raw={}) {
    ObjectHash hash;
    const auto add=[&hash](const std::string &value) {hash.update(std::to_string(value.size()));hash.update(":");hash.update(value);};
    const auto format=r.schemaVersion==1?"society-object-v1":"society-object-v2";
    add(format);add(container);add(r.key);add(std::to_string(r.indexKey));add(std::to_string(r.version));
    add(r.path);add(r.sha256);add(std::to_string(r.size));add(r.author.origin);add(r.author.subject);add(r.author.displayName);
    add(r.sessionKey);add(std::to_string(r.recordedAtNs));add(r.operation);add(r.deleted?"deleted":"live");add(r.parentValidationKey);
    add(s.device);add(s.description);add(std::to_string(s.startedAtNs));
    if(r.schemaVersion==2) {
        add(r.author.profile.format);add(r.author.profile.payload);
        add(r.authorship.format);add(r.authorship.payload);add(r.sourceStamp);
    }
    if(!context.empty()){add("iiFileProvider.WorkSession/1");add(context);}
    if(!raw.empty()){add("iiFileProvider.RawEnvelope/1");add(raw);}
    return std::string(format)+":"+hash.finish();
}
struct Chunk {std::string hash;std::uint64_t size=0;};
}

class ObjectStore::Impl {
public:
    sqlite3 *db=nullptr;
    std::string container;
    fs::path directory;
    mutable std::mutex mutex;
    int schema=0;
    bool readOnly=false;
    ~Impl() {if(db)sqlite3_close_v2(db);}
    void requireJournal() const {
        if(schema<4)fail("journal/RAW APIs require schema 4 or newer; open with a writer to migrate first");
    }
    std::string context(const std::string &key) const {
        if(schema<4)return {};
        Query q(db,"SELECT context FROM work_sessions WHERE session_key=?");q.bind(1,key);
        if(!q.row())fail("work-session context is missing");return q.text(0);
    }
    std::string rawDocument(const std::string &key,std::uint64_t version) const {
        if(schema<4)return {};
        Query q(db,"SELECT document FROM raw_revisions WHERE object_key=? AND version=?");
        q.bind(1,key);q.bind(2,static_cast<std::int64_t>(version));return q.row()?q.text(0):std::string{};
    }
    ObjectSession getSession(const std::string &key,bool active=false) const {
        Query q(db,"SELECT session_key,origin,subject,name,device,description,started,ended,profile_format,profile FROM sessions WHERE session_key=?");q.bind(1,key);
        if(!q.row())fail("object work session not found");
        ObjectSession s{q.text(0),{q.text(1),q.text(2),q.text(3)},q.text(4),q.text(5),q.number(6),q.number(7)};
        s.author.profile={q.text(8),q.text(9)};
        if(active && s.endedAtNs!=0)fail("object work session is closed");return s;
    }
    std::optional<ObjectRecord> get(const std::string &key,std::uint64_t version=0) const {
        Query q(db,std::string("SELECT ")+columns+join+"WHERE r.object_key=? AND r.version="+(version?"?":"o.head"));q.bind(1,key);
        if(version)q.bind(2,static_cast<std::int64_t>(version));if(!q.row())return {};return record(q);
    }
    ObjectRecord require(const std::string &key,std::uint64_t expected) const {
        const auto r=get(key);if(!r || r->deleted)fail("live object not found");
        if(expected==0 || r->version!=expected || expected>=INT64_MAX)fail("object version conflict");
        if(!validate(key,r->version,false))fail("object metadata chain is corrupt");return *r;
    }
    std::vector<Chunk> chunks(const std::string &key,std::uint64_t version) const {
        std::vector<Chunk> result;
        Query q(db,"SELECT ordinal,sha256,size FROM revision_chunks WHERE object_key=? AND version=? ORDER BY ordinal");
        q.bind(1,key);q.bind(2,static_cast<std::int64_t>(version));
        while(q.row()) {if(q.number(0)!=static_cast<std::int64_t>(result.size()))fail("non-contiguous object chunks");result.push_back({q.text(1),static_cast<std::uint64_t>(q.number(2))});}
        return result;
    }
    Chunk storeChunk(std::span<const std::uint8_t> bytes) {
        auto digest=ObjectHash::digest(bytes);
        Query existing(db,"SELECT data FROM chunks WHERE sha256=?");existing.bind(1,digest);
        if(existing.row()) {
            const auto previous=existing.bytes(0);
            if(previous.size()!=bytes.size() || !std::equal(previous.begin(),previous.end(),bytes.begin()))fail("corrupt existing object chunk");
        } else {Query insert(db,"INSERT INTO chunks(sha256,data) VALUES(?,?)");insert.bind(1,digest);insert.blob(2,bytes);insert.done();}
        return {std::move(digest),bytes.size()};
    }
    std::vector<Chunk> ingest(const detail::PreparedObjectSource &input,ObjectRecord &r,std::stop_token cancellation) {
        std::vector<Chunk> result;ObjectHash hash;
        for(std::size_t offset=0;offset<input.bytes.size();offset+=ObjectStore::chunkBytes){
            if(cancellation.stop_requested())fail("object packaging cancelled");
            const auto bytes=std::span(input.bytes).subspan(offset,std::min(ObjectStore::chunkBytes,input.bytes.size()-offset));
            hash.update(bytes);result.push_back(storeChunk(bytes));
        }
        if(cancellation.stop_requested())fail("object packaging cancelled");
        if(ObjectSource::inspect(input.source)!=input.identity)fail("object batch source changed before publication");
        r.sha256=hash.finish();r.size=input.bytes.size();return result;
    }
    std::vector<Chunk> ingest(const fs::path &input,ObjectRecord &r,std::stop_token cancellation) {
        const auto source=directPath(input,true);
        if(!fs::is_regular_file(source))fail("object source is not a regular file");
        const auto size=fs::file_size(source);const auto modified=fs::last_write_time(source);
        if(size>INT64_MAX)fail("object source exceeds format size");
        // libc++ defaults to a 4 KiB file buffer even when read() requests a
        // full chunk. Request chunk-sized native reads for image-backed drives.
        std::vector<char> inputBuffer(ObjectStore::chunkBytes);
        std::ifstream stream;stream.rdbuf()->pubsetbuf(inputBuffer.data(),inputBuffer.size());
        stream.open(source,std::ios::binary);if(!stream)fail("cannot read object source");
        std::vector<std::uint8_t> buffer(ObjectStore::chunkBytes);std::vector<Chunk> result;ObjectHash hash;std::uint64_t total=0;
        while(stream) {
            if(cancellation.stop_requested())fail("object packaging cancelled");
            stream.read(reinterpret_cast<char *>(buffer.data()),buffer.size());const auto n=static_cast<std::size_t>(stream.gcount());
            if(n==0)break;
            const std::span<const std::uint8_t> bytes(buffer.data(),n);hash.update(bytes);total+=n;
            if(total>size)fail("object source changed during packaging");
            result.push_back(storeChunk(bytes));
        }
        if(cancellation.stop_requested())fail("object packaging cancelled");
        if(stream.bad() || total!=size || directPath(input,true)!=source || fs::file_size(source)!=size || fs::last_write_time(source)!=modified)
            fail("object source changed during packaging");
        r.sha256=hash.finish();r.size=total;return result;
    }
    ObjectRecord publish(ObjectRecord r,const ObjectSession &s,const std::vector<Chunk> &parts,
                         std::optional<std::string> raw=std::nullopt) {
        const auto document=raw?*raw:(r.version>1?rawDocument(r.key,r.version-1):std::string{});
        r.author=s.author;r.sessionKey=s.key;r.recordedAtNs=now();r.schemaVersion=2;
        r.validationKey=validationKey(r,s,container,context(s.key),document);
        Query insert(db,"INSERT INTO revisions VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
        insert.bind(1,r.key);insert.bind(2,static_cast<std::int64_t>(r.version));insert.bind(3,r.path);insert.bind(4,r.sha256);insert.bind(5,static_cast<std::int64_t>(r.size));
        insert.bind(6,r.author.origin);insert.bind(7,r.author.subject);insert.bind(8,r.author.displayName);insert.bind(9,r.sessionKey);insert.bind(10,r.recordedAtNs);
        insert.bind(11,r.operation);insert.bind(12,static_cast<std::int64_t>(r.deleted));insert.bind(13,r.parentValidationKey);insert.bind(14,r.validationKey);
        insert.bind(15,r.authorship.format);insert.bind(16,r.authorship.payload);insert.bind(17,r.sourceStamp);insert.bind(18,std::int64_t(2));insert.done();
        if(!document.empty()) {
            Query rawInsert(db,"INSERT INTO raw_revisions VALUES(?,?,?)");rawInsert.bind(1,r.key);
            rawInsert.bind(2,static_cast<std::int64_t>(r.version));rawInsert.bind(3,document);rawInsert.done();
        }
        std::int64_t ordinal=0;
        for(const auto &part:parts) {
            Query q(db,"INSERT INTO revision_chunks VALUES(?,?,?,?,?)");q.bind(1,r.key);q.bind(2,static_cast<std::int64_t>(r.version));q.bind(3,ordinal++);q.bind(4,part.hash);q.bind(5,static_cast<std::int64_t>(part.size));q.done();
        }
        Query head(db,"UPDATE objects SET head=?,path=?,deleted=? WHERE object_key=?");head.bind(1,static_cast<std::int64_t>(r.version));head.bind(2,r.path);head.bind(3,static_cast<std::int64_t>(r.deleted));head.bind(4,r.key);head.done();return r;
    }
    ObjectRecord importFile(const fs::path &source,std::string path,const ObjectSession &session,const ObjectWriteOptions &options,
                            const detail::PreparedObjectSource *prepared=nullptr,const std::string &raw={}) {
        if(options.authorship)authorshipValid(*options.authorship);field(options.sourceStamp,32768,false);pathValid(path);
        if(options.authorship)validateDocument(db,*options.authorship);
        ObjectRecord r;r.key=uniqueKey(db,"object-");r.version=1;r.path=std::move(path);r.operation=raw.empty()?"import":"wrap_raw";
        r.authorship=options.authorship.value_or(ObjectMetadata{});r.sourceStamp=options.sourceStamp;
        Query create(db,"INSERT INTO objects(object_key,head,path,deleted) VALUES(?,0,?,0)");
        create.bind(1,r.key);create.bind(2,r.path);create.done();r.indexKey=sqlite3_last_insert_rowid(db);
        const auto parts=prepared?ingest(*prepared,r,options.cancellation):ingest(source,r,options.cancellation);
        return publish(std::move(r),session,parts,raw);
    }
    bool validate(const std::string &key,std::uint64_t version,bool payload) const {
        if(!version || version>INT64_MAX)return false;
        std::string previous;
        for(std::uint64_t v=1;v<=version;++v) {
            const auto r=get(key,v);if(!r)return false;const auto s=getSession(r->sessionKey);
            if(r->schemaVersion!=1 && r->schemaVersion!=2)return false;
            if(r->schemaVersion==1 && (!r->author.profile.payload.empty() || !r->authorship.payload.empty() || !r->sourceStamp.empty()))return false;
            if(r->version!=v || r->parentValidationKey!=previous || r->author!=s.author
                || validationKey(*r,s,container,context(s.key),rawDocument(key,v))!=r->validationKey)return false;
            previous=r->validationKey;
        }
        const auto r=*get(key,version);
        if(!payload)return true;
        if(r.deleted)return chunks(key,version).empty();
        ObjectHash hash;std::uint64_t total=0;
        const auto parts=chunks(key,version);
        if(parts.size()!=r.size/ObjectStore::chunkBytes+(r.size%ObjectStore::chunkBytes!=0))return false;
        for(std::size_t index=0;index<parts.size();++index) {
            const auto &part=parts[index];
            if(index+1<parts.size() && part.size!=ObjectStore::chunkBytes)return false;
            Query q(db,"SELECT data FROM chunks WHERE sha256=?");q.bind(1,part.hash);
            if(!q.row())return false;const auto bytes=q.bytes(0);
            if(bytes.size()!=part.size || bytes.empty() || bytes.size()>ObjectStore::chunkBytes || ObjectHash::digest(bytes)!=part.hash)return false;
            hash.update(bytes);total+=bytes.size();
        }
        return total==r.size && hash.finish()==r.sha256;
    }
};

ObjectStore::ObjectStore(const fs::path &directory,std::string container,bool create)
    :ObjectStore(directory,std::move(container),create,defaultWalAutoCheckpointPages) {}
ObjectStore::ObjectStore(const fs::path &directory,std::string container,bool create,int walAutoCheckpointPages)
    :ObjectStore(directory,std::move(container),create?Access::Create:Access::ReadWrite,walAutoCheckpointPages) {}
ObjectStore::ObjectStore(const fs::path &directory,std::string container,Access access,int walAutoCheckpointPages)
    :m_impl(std::make_unique<Impl>()) {
    if(access!=Access::ReadOnly && access!=Access::ReadWrite && access!=Access::Create)fail("invalid object package access mode");
    const bool create=access==Access::Create,readOnly=access==Access::ReadOnly;
    m_impl->readOnly=readOnly;
    if(walAutoCheckpointPages<0)fail("WAL auto-checkpoint page limit cannot be negative");
    field(container,1024);m_impl->container=std::move(container);
    auto root=directPath(directory,fs::exists(directory));
    m_impl->directory=root;
    if(!fs::exists(root)) {if(!create)fail("object package does not exist");if(!fs::create_directory(root))fail("cannot create object package");privatePermissions(root,true);}
    if(!fs::is_directory(root))fail("object package is not a directory");
    const auto path=directPath(root/"objects.sqlite3",false);bool fresh=false;
    if(!fs::exists(path)) {
        if(!create)fail("object package database does not exist");
        if(!fs::is_empty(root))fail("refusing to initialize a nonempty unknown package");
        FILE *file=exclusiveFile(path);if(!file)fail("object package creation conflict");std::fclose(file);privatePermissions(path);fresh=true;
    }
    for(const char *suffix:{"-wal","-shm","-journal"}) {
        fs::path sidecar=path;
        sidecar+=suffix;
        if(fs::is_symlink(fs::symlink_status(sidecar)))fail("redirected object database sidecar");
    }
    const auto utf8=path.u8string();
    int flags=(readOnly?SQLITE_OPEN_READONLY:SQLITE_OPEN_READWRITE)|SQLITE_OPEN_FULLMUTEX;
#ifdef SQLITE_OPEN_NOFOLLOW
    flags|=SQLITE_OPEN_NOFOLLOW;
#endif
    check(m_impl->db,sqlite3_open_v2(reinterpret_cast<const char *>(utf8.c_str()),&m_impl->db,flags,nullptr));
    auto *db=m_impl->db;sqlite3_busy_timeout(db,5000);
    const auto schema=scalar(db,"PRAGMA user_version");
    if(!fresh && (scalar(db,"PRAGMA application_id")!=applicationId || schema<1 || schema>5))fail("unknown object package format");
    m_impl->schema=static_cast<int>(schema);
    if(readOnly) {
        if(schema<3 || schema>5)fail("read-only object access requires schema 3, 4 or 5; migrate with a writer first");
        execute(db,"PRAGMA foreign_keys=ON; PRAGMA trusted_schema=OFF; PRAGMA query_only=ON;");
        Query identity(db,"SELECT container_key FROM store");
        if(!identity.row() || identity.text(0)!=m_impl->container || identity.row())fail("object package container mismatch");
        return;
    }
    execute(db,"PRAGMA foreign_keys=ON; PRAGMA trusted_schema=OFF; PRAGMA synchronous=FULL; PRAGMA fullfsync=ON;");
    std::unique_ptr<Transaction> migration;
    if(fresh || schema<5)migration=std::make_unique<Transaction>(db);
    if(fresh) {
        execute(db,R"SQL(
CREATE TABLE store(container_key TEXT NOT NULL);
CREATE TABLE sessions(session_key TEXT PRIMARY KEY,origin TEXT NOT NULL,subject TEXT NOT NULL,name TEXT NOT NULL,device TEXT NOT NULL,description TEXT NOT NULL,started INTEGER NOT NULL,ended INTEGER NOT NULL);
CREATE TABLE objects(index_key INTEGER PRIMARY KEY AUTOINCREMENT,object_key TEXT UNIQUE NOT NULL,head INTEGER NOT NULL,path TEXT NOT NULL,deleted INTEGER NOT NULL CHECK(deleted IN(0,1)));
CREATE UNIQUE INDEX live_paths ON objects(path) WHERE deleted=0;
CREATE INDEX live_index ON objects(deleted,index_key);
CREATE TABLE revisions(object_key TEXT NOT NULL REFERENCES objects(object_key),version INTEGER NOT NULL CHECK(version>0),path TEXT NOT NULL,sha256 TEXT NOT NULL,size INTEGER NOT NULL CHECK(size>=0),author_origin TEXT NOT NULL,author_subject TEXT NOT NULL,author_name TEXT NOT NULL,session_key TEXT NOT NULL REFERENCES sessions(session_key),at_ns INTEGER NOT NULL,operation TEXT NOT NULL,deleted INTEGER NOT NULL CHECK(deleted IN(0,1)),parent_key TEXT NOT NULL,validation_key TEXT NOT NULL,PRIMARY KEY(object_key,version));
CREATE TABLE chunks(sha256 TEXT PRIMARY KEY,data BLOB NOT NULL);
CREATE TABLE revision_chunks(object_key TEXT NOT NULL,version INTEGER NOT NULL,ordinal INTEGER NOT NULL CHECK(ordinal>=0),sha256 TEXT NOT NULL REFERENCES chunks(sha256),size INTEGER NOT NULL CHECK(size>0),PRIMARY KEY(object_key,version,ordinal),FOREIGN KEY(object_key,version) REFERENCES revisions(object_key,version));
PRAGMA application_id=1397703242;
PRAGMA user_version=1;
)SQL");
        Query q(db,"INSERT INTO store VALUES(?)");q.bind(1,m_impl->container);q.done();
    }
    {Query q(db,"SELECT container_key FROM store");if(!q.row() || q.text(0)!=m_impl->container || q.row())fail("object package container mismatch");}
    if(fresh || schema==1) {
        execute(db,R"SQL(
ALTER TABLE sessions ADD COLUMN profile_format TEXT NOT NULL DEFAULT '';
ALTER TABLE sessions ADD COLUMN profile TEXT NOT NULL DEFAULT '';
ALTER TABLE revisions ADD COLUMN metadata_format TEXT NOT NULL DEFAULT '';
ALTER TABLE revisions ADD COLUMN metadata TEXT NOT NULL DEFAULT '';
ALTER TABLE revisions ADD COLUMN source_stamp TEXT NOT NULL DEFAULT '';
ALTER TABLE revisions ADD COLUMN record_format INTEGER NOT NULL DEFAULT 1 CHECK(record_format IN(1,2));
PRAGMA user_version=2;
)SQL");
    }
    if(fresh || schema<3) {
        // Keep the traversal working set physically separate from large author
        // documents, revision history and payload. Publication updates objects
        // only after inserting its revision; this trigger shares that transaction.
        execute(db,R"SQL(
CREATE TABLE current_index(
 index_key INTEGER PRIMARY KEY REFERENCES objects(index_key),
 object_key TEXT NOT NULL,head INTEGER NOT NULL,path TEXT NOT NULL,
 sha256 TEXT NOT NULL,size INTEGER NOT NULL,validation_key TEXT NOT NULL,
 session_key TEXT NOT NULL,deleted INTEGER NOT NULL CHECK(deleted IN(0,1)),
 author_origin TEXT NOT NULL,author_subject TEXT NOT NULL);
INSERT INTO current_index
 SELECT o.index_key,o.object_key,o.head,o.path,r.sha256,r.size,r.validation_key,
        r.session_key,o.deleted,r.author_origin,r.author_subject
 FROM objects o JOIN revisions r ON r.object_key=o.object_key AND r.version=o.head;
CREATE TRIGGER current_index_publish AFTER UPDATE OF head,path,deleted ON objects
WHEN NEW.head>0 BEGIN
 INSERT OR REPLACE INTO current_index
 SELECT NEW.index_key,NEW.object_key,NEW.head,NEW.path,r.sha256,r.size,
        r.validation_key,r.session_key,NEW.deleted,r.author_origin,r.author_subject
 FROM revisions r WHERE r.object_key=NEW.object_key AND r.version=NEW.head;
END;
PRAGMA user_version=3;
)SQL");
    }
    if(fresh || schema<4) {
        execute(db,R"SQL(
CREATE TABLE IF NOT EXISTS work_sessions(
 sequence INTEGER PRIMARY KEY AUTOINCREMENT,
 session_key TEXT UNIQUE NOT NULL REFERENCES sessions(session_key),context TEXT NOT NULL DEFAULT '');
INSERT OR IGNORE INTO work_sessions(session_key) SELECT session_key FROM sessions ORDER BY started,rowid;
CREATE TABLE IF NOT EXISTS raw_revisions(
 object_key TEXT NOT NULL,version INTEGER NOT NULL,document TEXT NOT NULL,
 PRIMARY KEY(object_key,version),FOREIGN KEY(object_key,version) REFERENCES revisions(object_key,version));
CREATE TABLE IF NOT EXISTS object_journal(
 sequence INTEGER PRIMARY KEY AUTOINCREMENT,object_key TEXT NOT NULL,version INTEGER NOT NULL,
 session_key TEXT NOT NULL REFERENCES sessions(session_key),UNIQUE(object_key,version),
 FOREIGN KEY(object_key,version) REFERENCES revisions(object_key,version));
INSERT OR IGNORE INTO object_journal(object_key,version,session_key)
 SELECT object_key,version,session_key FROM revisions ORDER BY at_ns,rowid;
CREATE INDEX IF NOT EXISTS object_journal_object ON object_journal(object_key,sequence);
CREATE INDEX IF NOT EXISTS object_journal_session ON object_journal(session_key,sequence);
CREATE TRIGGER IF NOT EXISTS object_journal_append AFTER INSERT ON revisions BEGIN
 INSERT INTO object_journal(object_key,version,session_key) VALUES(NEW.object_key,NEW.version,NEW.session_key);
END;
PRAGMA user_version=4;
)SQL");
    }
    if(fresh || schema<5)execute(db,detail::ontologySchema);
    if(migration)migration->commit();
    m_impl->schema=5;
    {Query q(db,"PRAGMA journal_mode=WAL");if(!q.row() || q.text(0)!="wal")fail("object package requires WAL journaling");}
    execute(db,"PRAGMA synchronous=FULL; PRAGMA fullfsync=ON;");
    check(db,sqlite3_wal_autocheckpoint(db,walAutoCheckpointPages));
}
ObjectStore::~ObjectStore()=default;
const fs::path &ObjectStore::directory() const noexcept{return m_impl->directory;}
const std::string &ObjectStore::containerKey() const noexcept{return m_impl->container;}
ObjectStore::Access ObjectStore::access() const noexcept{return m_impl->readOnly?Access::ReadOnly:Access::ReadWrite;}
ObjectSession ObjectStore::beginSession(ObjectAuthor author,std::string device,std::string description) {
    return beginSession(std::move(author),std::move(device),std::move(description),{});
}
ObjectSession ObjectStore::beginSession(ObjectAuthor author,std::string device,std::string description,const ObjectSessionContext &context) {
    authorValid(author);field(device,4096);field(description,16384,false);
    const auto document=detail::encodeSessionContext(context);
    std::lock_guard lock(m_impl->mutex);validateActor(m_impl->db,author);Transaction transaction(m_impl->db);
    ObjectSession s{uniqueKey(m_impl->db,"session-"),std::move(author),std::move(device),std::move(description),now(),0};
    Query q(m_impl->db,"INSERT INTO sessions VALUES(?,?,?,?,?,?,?,0,?,?)");q.bind(1,s.key);q.bind(2,s.author.origin);q.bind(3,s.author.subject);q.bind(4,s.author.displayName);q.bind(5,s.device);q.bind(6,s.description);q.bind(7,s.startedAtNs);
    q.bind(8,s.author.profile.format);q.bind(9,s.author.profile.payload);q.done();
    Query details(m_impl->db,"INSERT INTO work_sessions(session_key,context) VALUES(?,?)");
    details.bind(1,s.key);details.bind(2,document);details.done();transaction.commit();return s;
}
void ObjectStore::endSession(const std::string &key) {
    std::lock_guard lock(m_impl->mutex);Transaction transaction(m_impl->db);m_impl->getSession(key,true);
    Query q(m_impl->db,"UPDATE sessions SET ended=? WHERE session_key=?");q.bind(1,now());q.bind(2,key);q.done();transaction.commit();
}
ObjectSession ObjectStore::session(const std::string &key) const {std::lock_guard lock(m_impl->mutex);return m_impl->getSession(key);}
WorkSessionRecord ObjectStore::workSession(const std::string &key) const {
    field(key,1024);std::lock_guard lock(m_impl->mutex);m_impl->requireJournal();Transaction snapshot(m_impl->db,false);
    Query q(m_impl->db,"SELECT sequence,context FROM work_sessions WHERE session_key=?");q.bind(1,key);
    if(!q.row())fail("work session not found");
    WorkSessionRecord result{q.number(0),m_impl->getSession(key),detail::decodeSessionContext(q.text(1))};snapshot.commit();return result;
}
std::vector<WorkSessionRecord> ObjectStore::sessions(const ObjectSessionQuery &query) const {
    if(query.afterSequence<0 || !query.limit || query.limit>128)fail("invalid work-session page");
    std::lock_guard lock(m_impl->mutex);m_impl->requireJournal();Transaction snapshot(m_impl->db,false);
    Query q(m_impl->db,std::string("SELECT w.sequence,w.session_key,w.context FROM work_sessions w JOIN sessions s ON s.session_key=w.session_key WHERE w.sequence>? ")
        +(query.activeOnly?"AND s.ended=0 ":"")+"ORDER BY w.sequence LIMIT ?");
    q.bind(1,query.afterSequence);q.bind(2,static_cast<std::int64_t>(query.limit));
    std::vector<WorkSessionRecord> result;std::size_t bytes=0;
    while(q.row()) {
        const auto document=q.text(2);auto session=m_impl->getSession(q.text(1));
        const auto amount=document.size()+session.author.profile.payload.size()+session.author.displayName.size()
            +session.device.size()+session.description.size()+session.author.origin.size()+session.author.subject.size()+session.key.size();
        if(bytes+amount>16*1024*1024)break;
        bytes+=amount;result.push_back({q.number(0),std::move(session),detail::decodeSessionContext(document)});
    }
    snapshot.commit();return result;
}
std::vector<ObjectJournalEntry> ObjectStore::journal(const ObjectJournalQuery &query) const {
    if(query.afterSequence<0 || !query.limit || query.limit>1024)fail("invalid object journal page");
    field(query.objectKey,1024,false);field(query.sessionKey,1024,false);
    std::lock_guard lock(m_impl->mutex);m_impl->requireJournal();
    std::string sql="SELECT j.sequence,r.object_key,r.version,r.session_key,r.at_ns,r.operation,r.path,r.sha256,r.size,r.deleted,r.validation_key FROM object_journal j JOIN revisions r ON r.object_key=j.object_key AND r.version=j.version WHERE j.sequence>? ";
    int next=2;
    if(!query.objectKey.empty())sql+="AND j.object_key=? ";
    if(!query.sessionKey.empty())sql+="AND j.session_key=? ";
    sql+="ORDER BY j.sequence LIMIT ?";Query q(m_impl->db,sql);q.bind(1,query.afterSequence);
    if(!query.objectKey.empty())q.bind(next++,query.objectKey);
    if(!query.sessionKey.empty())q.bind(next++,query.sessionKey);
    q.bind(next,static_cast<std::int64_t>(query.limit));std::vector<ObjectJournalEntry> result;
    while(q.row())result.push_back({q.number(0),q.text(1),static_cast<std::uint64_t>(q.number(2)),q.text(3),q.number(4),q.text(5),q.text(6),q.text(7),static_cast<std::uint64_t>(q.number(8)),q.number(9)!=0,q.text(10)});
    return result;
}
ObjectRecord ObjectStore::wrapRaw(const fs::path &source,std::string path,const RawMetadata &metadata,const std::string &session,const ObjectWriteOptions &options) {
    const auto document=detail::encodeRawMetadata(metadata);pathValid(path);
    if(options.cancellation.stop_requested())fail("RAW wrapping cancelled");
    {
        std::lock_guard lock(m_impl->mutex);m_impl->requireJournal();
        if(m_impl->readOnly)fail("cannot wrap RAW through a read-only package");
        (void)m_impl->getSession(session,true);
    }
    // Stable descriptor snapshot rejects redirected or changing sources and
    // keeps the caller's original file untouched. Memory remains chunk-bounded.
    ObjectSource captured(source,m_impl->directory,options.cancellation);
    auto capturedOptions=options;
    if(capturedOptions.sourceStamp.empty())capturedOptions.sourceStamp=captured.identity().stamp;
    std::lock_guard lock(m_impl->mutex);m_impl->requireJournal();Transaction transaction(m_impl->db);
    const auto s=m_impl->getSession(session,true);
    auto result=m_impl->importFile(captured.path(),std::move(path),s,capturedOptions,nullptr,document);
    transaction.commit();return result;
}
RawEnvelope ObjectStore::rawEnvelope(const std::string &key,std::uint64_t version) const {
    if(version>INT64_MAX)fail("invalid RAW revision");
    std::lock_guard lock(m_impl->mutex);m_impl->requireJournal();Transaction snapshot(m_impl->db,false);
    const auto record=m_impl->get(key,version);if(!record)fail("RAW envelope not found");
    if(!m_impl->validate(key,record->version,false))fail("RAW metadata chain is corrupt");
    const auto document=m_impl->rawDocument(key,record->version);if(document.empty())fail("object is not a RAW envelope");
    RawEnvelope result{*record,detail::decodeRawMetadata(document)};snapshot.commit();return result;
}
ObjectDocument ObjectStore::importDocument(const fs::path &source,std::string path,const RawMetadata &metadata,
    const std::string &session,const ObjectWriteOptions &options) {
    const auto document=detail::encodeRawMetadata(metadata);pathValid(path);
    if(path.starts_with(".originals/"))fail("document path uses the original namespace");
    if(options.cancellation.stop_requested())fail("document capture cancelled");
    {
        std::lock_guard lock(m_impl->mutex);m_impl->requireJournal();
        if(m_impl->readOnly)fail("cannot capture through a read-only package");
        (void)m_impl->getSession(session,true);
    }
    ObjectSource captured(source,m_impl->directory,options.cancellation);
    auto capturedOptions=options;if(capturedOptions.sourceStamp.empty())capturedOptions.sourceStamp=captured.identity().stamp;
    std::lock_guard lock(m_impl->mutex);Transaction transaction(m_impl->db);
    const auto s=m_impl->getSession(session,true);
    ObjectRecord working;working.key=uniqueKey(m_impl->db,"object-");working.version=1;
    working.path=std::move(path);working.operation="import";
    working.authorship=options.authorship.value_or(ObjectMetadata{});working.sourceStamp=capturedOptions.sourceStamp;
    const auto raw=m_impl->importFile(captured.path(),".originals/"+working.key,s,capturedOptions,nullptr,document);
    working.size=raw.size;working.sha256=raw.sha256;
    Query create(m_impl->db,"INSERT INTO objects(object_key,head,path,deleted) VALUES(?,0,?,0)");
    create.bind(1,working.key);create.bind(2,working.path);create.done();working.indexKey=sqlite3_last_insert_rowid(m_impl->db);
    working=m_impl->publish(std::move(working),s,m_impl->chunks(raw.key,raw.version));
    if(options.cancellation.stop_requested())fail("document capture cancelled");
    transaction.commit();return {working,{raw,metadata}};
}
RawEnvelope ObjectStore::original(const std::string &key) const {
    field(key,1024);if(key.find('/')!=std::string::npos || key.find('\\')!=std::string::npos)fail("invalid document key");
    const auto raw=lookupPath(".originals/"+key);if(!raw)fail("document original not found");
    return rawEnvelope(raw->key);
}
ObjectRecord ObjectStore::reviseMetadata(const std::string &key,const FileMetadata &metadata,std::uint64_t expected,const std::string &session) {
    std::lock_guard lock(m_impl->mutex);m_impl->requireJournal();Transaction transaction(m_impl->db);
    auto r=m_impl->require(key,expected);const auto s=m_impl->getSession(session,true);
    const auto previous=m_impl->rawDocument(key,r.version);if(previous.empty())fail("object is not a RAW envelope");
    auto raw=detail::decodeRawMetadata(previous);raw.annotations=metadata;
    const auto document=detail::encodeRawMetadata(raw);
    if(document==previous){transaction.commit();return r;}
    const auto parts=m_impl->chunks(key,r.version);r.parentValidationKey=r.validationKey;++r.version;r.operation="metadata";
    r=m_impl->publish(std::move(r),s,parts,document);transaction.commit();return r;
}
std::vector<MetadataChange> ObjectStore::metadataDiff(const std::string &key,std::uint64_t version) const {
    if(!version || version>INT64_MAX)fail("invalid RAW metadata diff revision");
    std::lock_guard lock(m_impl->mutex);m_impl->requireJournal();Transaction snapshot(m_impl->db,false);
    if(!m_impl->validate(key,version,false))fail("RAW metadata chain is corrupt");
    const auto after=m_impl->rawDocument(key,version);if(after.empty())fail("object is not a RAW envelope");
    const auto before=version>1?m_impl->rawDocument(key,version-1):std::string{};
    auto result=detail::metadataChanges(before,after);snapshot.commit();return result;
}
ObjectRecord ObjectStore::importFile(const fs::path &source,std::string path,const std::string &session,const ObjectWriteOptions &options) {
    std::lock_guard lock(m_impl->mutex);Transaction transaction(m_impl->db);const auto s=m_impl->getSession(session,true);
    auto record=m_impl->importFile(source,std::move(path),s,options);transaction.commit();return record;
}
std::vector<ObjectRecord> ObjectStore::importFiles(const std::vector<ObjectImport> &files,const std::string &session) {
    if(files.empty() || files.size()>maximumBatchFiles)fail("object batch must contain 1 to 256 files");
    std::lock_guard lock(m_impl->mutex);Transaction transaction(m_impl->db);const auto s=m_impl->getSession(session,true);
    // Preflight retained documents before reading payloads or publishing rows.
    // A higher record count must not multiply large actor/provenance documents
    // into an unbounded result vector.
    std::size_t metadataRemaining=maximumBatchMetadataBytes;
    for(const auto &file:files){
        if(file.options.cancellation.stop_requested())fail("object packaging cancelled");
        const auto actorBytes=s.author.profile.payload.size();
        const auto provenanceBytes=file.options.authorship?file.options.authorship->payload.size():0;
        if(actorBytes>metadataRemaining)fail("object batch metadata exceeds 16 MiB");
        metadataRemaining-=actorBytes;
        if(provenanceBytes>metadataRemaining)fail("object batch metadata exceeds 16 MiB");
        metadataRemaining-=provenanceBytes;
    }
    const auto prepared=detail::readObjectBatch(files);
    std::vector<ObjectRecord> records;records.reserve(files.size());
    for(std::size_t i=0;i<files.size();++i){
        const auto &file=files[i];
        if(file.options.cancellation.stop_requested())fail("object packaging cancelled");
        records.push_back(m_impl->importFile(prepared[i].source,file.logicalPath,s,file.options,&prepared[i]));
    }
    for(const auto &file:files)if(file.options.cancellation.stop_requested())fail("object packaging cancelled");
    transaction.commit();return records;
}
ObjectRecord ObjectStore::reviseFile(const std::string &key,const fs::path &source,std::uint64_t expected,const std::string &session,const ObjectWriteOptions &options) {
    if(options.authorship)authorshipValid(*options.authorship);field(options.sourceStamp,32768,false);
    std::lock_guard lock(m_impl->mutex);Transaction transaction(m_impl->db);auto r=m_impl->require(key,expected);const auto s=m_impl->getSession(session,true);
    if(!m_impl->rawDocument(key,r.version).empty())fail("RAW payload is immutable; revise metadata or wrap a new original");
    if(options.authorship)validateDocument(m_impl->db,*options.authorship);
    if(options.authorship)r.authorship=*options.authorship;r.sourceStamp=options.sourceStamp;
    r.parentValidationKey=r.validationKey;++r.version;r.operation="revise";const auto chunks=m_impl->ingest(source,r,options.cancellation);
    r=m_impl->publish(std::move(r),s,chunks);transaction.commit();return r;
}
ObjectRecord ObjectStore::move(const std::string &key,std::string path,std::uint64_t expected,const std::string &session) {
    return move(key,std::move(path),expected,session,{});
}
ObjectRecord ObjectStore::move(const std::string &key,std::string path,std::uint64_t expected,const std::string &session,const ObjectWriteOptions &options) {
    field(options.sourceStamp,32768,false);if(options.cancellation.stop_requested())fail("object move cancelled");
    if(options.authorship)fail("move preserves original authorship");
    pathValid(path);std::lock_guard lock(m_impl->mutex);Transaction transaction(m_impl->db);auto r=m_impl->require(key,expected);const auto s=m_impl->getSession(session,true);
    if(!options.sourceStamp.empty())r.sourceStamp=options.sourceStamp;
    const auto chunks=m_impl->chunks(key,r.version);r.path=std::move(path);r.parentValidationKey=r.validationKey;++r.version;r.operation="move";
    r=m_impl->publish(std::move(r),s,chunks);transaction.commit();return r;
}
ObjectRecord ObjectStore::erase(const std::string &key,std::uint64_t expected,const std::string &session) {
    std::lock_guard lock(m_impl->mutex);Transaction transaction(m_impl->db);auto r=m_impl->require(key,expected);const auto s=m_impl->getSession(session,true);
    r.parentValidationKey=r.validationKey;++r.version;r.operation="erase";r.deleted=true;r=m_impl->publish(std::move(r),s,{});transaction.commit();return r;
}
std::optional<ObjectRecord> ObjectStore::lookup(const std::string &key) const {std::lock_guard lock(m_impl->mutex);return m_impl->get(key);}
std::optional<ObjectRecord> ObjectStore::lookupPath(const std::string &path) const {
    pathValid(path);std::lock_guard lock(m_impl->mutex);
    Query q(m_impl->db,std::string("SELECT ")+columns+join+"WHERE o.path=? AND o.deleted=0 AND r.version=o.head");q.bind(1,path);if(!q.row())return {};return record(q);
}
std::vector<ObjectRecord> ObjectStore::scan(std::int64_t after,std::size_t limit,bool deleted) const {
    if(after<0 || !limit || limit>10000)fail("invalid object index page");std::lock_guard lock(m_impl->mutex);
    Query q(m_impl->db,std::string("SELECT ")+columns+join+"WHERE o.index_key>? AND r.version=o.head "+(deleted?"":"AND o.deleted=0 ")+"ORDER BY o.index_key LIMIT ?");q.bind(1,after);q.bind(2,static_cast<std::int64_t>(limit));
    std::vector<ObjectRecord> result;while(q.row())result.push_back(record(q));return result;
}
std::vector<ObjectRecord> ObjectStore::history(const std::string &key,std::uint64_t after,std::size_t limit) const {
    if(after>INT64_MAX || !limit || limit>10000)fail("invalid object history page");std::lock_guard lock(m_impl->mutex);
    Query q(m_impl->db,std::string("SELECT ")+columns+join+"WHERE r.object_key=? AND r.version>? ORDER BY r.version LIMIT ?");q.bind(1,key);q.bind(2,static_cast<std::int64_t>(after));q.bind(3,static_cast<std::int64_t>(limit));
    std::vector<ObjectRecord> result;while(q.row())result.push_back(record(q));return result;
}
std::vector<ObjectIndexEntry> ObjectStore::index(std::int64_t after,std::size_t limit,bool deleted) const {
    if(after<0 || !limit || limit>10000)fail("invalid object index page");std::lock_guard lock(m_impl->mutex);
    Query q(m_impl->db,std::string("SELECT object_key,index_key,head,path,sha256,size,validation_key,session_key,deleted,author_origin,author_subject FROM current_index WHERE index_key>? ")
        +(deleted?"":"AND deleted=0 ")+"ORDER BY index_key LIMIT ?");
    q.bind(1,after);q.bind(2,static_cast<std::int64_t>(limit));std::vector<ObjectIndexEntry> result;
    while(q.row())result.push_back({q.text(0),q.number(1),static_cast<std::uint64_t>(q.number(2)),q.text(3),q.text(4),static_cast<std::uint64_t>(q.number(5)),q.text(6),q.text(7),q.number(8)!=0,q.text(9),q.text(10)});
    return result;
}
std::vector<ObjectChange> ObjectStore::diff(const std::string &key,std::uint64_t version) const {
    std::lock_guard lock(m_impl->mutex);Transaction snapshot(m_impl->db,false);
    if(!version || version>INT64_MAX || !m_impl->get(key,version))fail("object version not found");
    const auto before=version>1?m_impl->chunks(key,version-1):std::vector<Chunk>{};const auto after=m_impl->chunks(key,version);
    std::vector<ObjectChange> result;
    for(std::size_t i=0;i<std::max(before.size(),after.size());++i) {
        const auto a=i<before.size()?before[i]:Chunk{},b=i<after.size()?after[i]:Chunk{};
        if(a.hash!=b.hash || a.size!=b.size)result.push_back({i*chunkBytes,a.hash,b.hash,a.size,b.size});
    }
    snapshot.commit();return result;
}
bool ObjectStore::validate(const std::string &key,std::uint64_t version,bool payload) const {
    std::lock_guard lock(m_impl->mutex);Transaction snapshot(m_impl->db,false);const auto result=m_impl->validate(key,version,payload);snapshot.commit();return result;
}
std::uint64_t ObjectStore::count(bool deleted) const {std::lock_guard lock(m_impl->mutex);return scalar(m_impl->db,deleted?"SELECT count(*) FROM objects":"SELECT count(*) FROM objects WHERE deleted=0");}
void ObjectStore::extract(const std::string &key,std::uint64_t version,const fs::path &destination) const {
    std::lock_guard lock(m_impl->mutex);Transaction snapshot(m_impl->db,false);
    if(!m_impl->validate(key,version,true))fail("object integrity validation failed");
    const auto r=m_impl->get(key,version);if(!r || r->deleted)fail("cannot extract absent object content");
    const auto output=directPath(destination,false);if(fs::exists(output))fail("extraction destination exists");
    TemporaryLease lease(output.parent_path(),TemporaryLease::Mode::Shared);
    const auto temporary=output.parent_path()/uniqueKey(m_impl->db,".society-extract-");
    FILE *raw=exclusiveFile(temporary);if(!raw)fail("cannot stage object extraction");
    struct Cleanup {fs::path path;FILE *file;~Cleanup(){if(file)std::fclose(file);std::error_code ec;fs::remove(path,ec);}} cleanup{temporary,raw};
    privatePermissions(temporary);
    for(const auto &part:m_impl->chunks(key,version)) {
        Query q(m_impl->db,"SELECT data FROM chunks WHERE sha256=?");q.bind(1,part.hash);if(!q.row())fail("missing object chunk");const auto bytes=q.bytes(0);
        if(std::fwrite(bytes.data(),1,bytes.size(),raw)!=bytes.size())fail("cannot write object extraction");
    }
    if(std::fflush(raw)!=0)fail("cannot flush object extraction");
#ifdef _WIN32
    if(_commit(_fileno(raw))!=0)fail("cannot synchronize object extraction");
#else
    if(fsync(fileno(raw))!=0)fail("cannot synchronize object extraction");
#endif
    if(std::fclose(raw)!=0){cleanup.file=nullptr;fail("cannot close object extraction");}cleanup.file=nullptr;
    fs::create_hard_link(temporary,output); // Atomic exclusive publication; no overwrite race.
    snapshot.commit();
}
}
