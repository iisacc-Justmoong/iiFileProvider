#include "OntologyFileSystem.h"
#include "ObjectSqlite_p.h"
#include "ObjectHash.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace iiFileProvider {
namespace {
using Json = nlohmann::json;
using namespace storage_detail;
namespace fs = std::filesystem;
void text(const std::string &s, std::size_t maximum = 1024, bool required = true) {
    if ((required && s.empty()) || s.size() > maximum || s.find('\0') != std::string::npos)
        fail("invalid ontology text or byte limit");
    (void)Json(s).dump();
}
void pageLimit(std::size_t limit) { if (!limit || limit > 1024) fail("invalid ontology page limit"); }
std::string nodeKey(const OntologyResource &resource) {
    text(resource.key);
    switch (resource.kind) {
    case OntologyResourceKind::File: return "file:" + resource.key;
    case OntologyResourceKind::Entity: return "entity:" + resource.key;
    }
    fail("invalid ontology resource kind");
}
OntologyResource resourceKey(const std::string &key) {
    if (key.starts_with("file:")) return OntologyResource::file(key.substr(5));
    if (key.starts_with("entity:")) return OntologyResource::entity(key.substr(7));
    fail("corrupt ontology resource key");
}
Json valueJson(const OntologyValue &value) {
    return std::visit([](const auto &v)->Json {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, OntologyResource>) return nodeKey(v);
        else if constexpr (std::is_same_v<T, std::string>) { text(v, 65536, false); return v; }
        else if constexpr (std::is_same_v<T, double>) { if (!std::isfinite(v)) fail("non-finite ontology real"); return v==0.0?0.0:v; }
        else return v;
    }, value.data);
}
OntologyValue decodeValue(int kind, const std::string &document) {
    if (document.size() > 400000) fail("oversize ontology value");
    auto j = Json::parse(document); OntologyValue result;
    switch (static_cast<OntologyValueKind>(kind)) {
    case OntologyValueKind::Resource: result = OntologyValue::resource(resourceKey(j.get<std::string>())); break;
    case OntologyValueKind::Text: result = OntologyValue::text(j.get<std::string>()); break;
    case OntologyValueKind::Integer:
        if ((!j.is_number_integer() || j.is_number_float()) || (j.is_number_unsigned() && j.get<std::uint64_t>() > INT64_MAX)) fail("invalid ontology integer");
        result = OntologyValue::integer(j.get<std::int64_t>()); break;
    case OntologyValueKind::Real: if (!j.is_number_float()) fail("invalid ontology real"); result = OntologyValue::real(j.get<double>()); break;
    case OntologyValueKind::Boolean: result = OntologyValue::boolean(j.get<bool>()); break;
    default: fail("invalid ontology value kind");
    }
    if (valueJson(result).dump() != document) fail("noncanonical ontology value");
    return result;
}
Json classJson(const OntologyClass &c) {
    text(c.key); text(c.label, 4096, false); text(c.description, 16384, false);
    if (c.parents.size() > 32) fail("too many direct ontology parents");
    auto parents = c.parents;
    for (const auto &p : parents) text(p);
    std::sort(parents.begin(), parents.end());
    if (std::adjacent_find(parents.begin(), parents.end()) != parents.end()) fail("duplicate ontology parent");
    return Json{{"key",c.key},{"label",c.label},{"description",c.description},{"parents",parents}};
}
Json propertyJson(const OntologyProperty &p) {
    text(p.key); text(p.label, 4096, false); text(p.domainClass, 1024, false); text(p.rangeClass, 1024, false);
    if (static_cast<unsigned>(p.kind) > 4 || (p.kind != OntologyValueKind::Resource && (!p.rangeClass.empty() || p.acyclic)))
        fail("invalid ontology property definition");
    return Json{{"key",p.key},{"label",p.label},{"kind",static_cast<int>(p.kind)},
        {"domain",p.domainClass},{"range",p.rangeClass},{"single",p.singleValue},{"acyclic",p.acyclic}};
}
OntologyProperty decodeProperty(const std::string &document) {
    auto j = Json::parse(document);
    OntologyProperty result{j.at("key").get<std::string>(),j.at("label").get<std::string>(),
        static_cast<OntologyValueKind>(j.at("kind").get<int>()),j.at("domain").get<std::string>(),
        j.at("range").get<std::string>(),j.at("single").get<bool>(),j.at("acyclic").get<bool>()};
    if (propertyJson(result).dump() != document) fail("noncanonical ontology property");
    return result;
}
Json queryJson(const OntologyQuery &q) {
    text(q.classKey, 1024, false);
    if (q.conditions.size() > 16) fail("too many ontology query conditions");
    auto conditions = Json::array();
    for (const auto &c : q.conditions) {
        text(c.propertyKey);
        Json item{{"property",c.propertyKey}};
        if (c.equals) { item["kind"] = static_cast<int>(c.equals->kind()); item["value"] = valueJson(*c.equals); }
        conditions.push_back(std::move(item));
    }
    Json result{{"class",q.classKey},{"subclasses",q.includeSubclasses},{"deleted",q.includeDeleted},{"conditions",conditions}};
    if (result.dump().size() > 256 * 1024) fail("ontology query exceeds 256 KiB");
    return result;
}
OntologyQuery decodeQuery(const Json &j) {
    OntologyQuery result; result.classKey = j.at("class").get<std::string>();
    result.includeSubclasses = j.at("subclasses").get<bool>(); result.includeDeleted = j.at("deleted").get<bool>();
    for (const auto &item : j.at("conditions")) {
        OntologyCondition c; c.propertyKey = item.at("property").get<std::string>();
        if (item.contains("value")) c.equals = decodeValue(item.at("kind").get<int>(), item.at("value").dump());
        result.conditions.push_back(std::move(c));
    }
    if (queryJson(result) != j) fail("invalid saved ontology query");
    return result;
}
void directoryPath(const std::string &path) {
    text(path, 4096);
    if (path.front() == '/' || path.back() == '/' || path.find_first_of("\\:") != std::string::npos) fail("ontology directory path must be relative");
    std::size_t start = 0;
    while (start < path.size()) {
        const auto end = path.find('/', start); const auto part = path.substr(start, end-start);
        if (part.empty() || part == "." || part == "..") fail("ontology directory traversal");
        if (end == std::string::npos) break; start = end+1;
    }
}
struct Progress {
    sqlite3 *db; std::stop_token cancellation;
    Progress(sqlite3 *connection, std::stop_token token):db(connection),cancellation(token) {
        if (token.stop_requested()) fail("ontology operation cancelled");
        sqlite3_progress_handler(db, 1000, [](void *p) { return static_cast<Progress *>(p)->cancellation.stop_requested()?1:0; }, this);
    }
    ~Progress() { sqlite3_progress_handler(db, 0, nullptr, nullptr); }
    void check() const { if (cancellation.stop_requested()) fail("ontology operation cancelled"); }
};
ObjectIndexEntry indexEntry(Query &q) {
    return {q.text(0),q.number(1),static_cast<std::uint64_t>(q.number(2)),q.text(3),q.text(4),
        static_cast<std::uint64_t>(q.number(5)),q.text(6),q.text(7),q.number(8)!=0,q.text(9),q.text(10)};
}
}

class OntologyFileSystem::Impl {
public:
    sqlite3 *db = nullptr;
    fs::path directory;
    std::string container;
    bool readOnly = true;
    mutable std::mutex mutex;
    ~Impl() { if (db) sqlite3_close_v2(db); }
    std::uint64_t revision() const { return static_cast<std::uint64_t>(scalar(db, "SELECT revision FROM ontology_state WHERE id=1")); }
    void classExists(const std::string &key) const {
        if (key.empty()) return;
        Query q(db,"SELECT 1 FROM ontology_classes WHERE key=?"); q.bind(1,key); if (!q.row()) fail("ontology class not found");
    }
    std::optional<OntologyClass> getClass(const std::string &key) const {
        Query q(db,"SELECT label,description FROM ontology_classes WHERE key=?"); q.bind(1,key);
        if (!q.row()) return {};
        OntologyClass result{key,q.text(0),q.text(1),{}};
        Query parents(db,"SELECT parent FROM ontology_parents WHERE child=? ORDER BY parent"); parents.bind(1,key);
        while (parents.row()) result.parents.push_back(parents.text(0));
        return result;
    }
    std::optional<OntologyProperty> getProperty(const std::string &key) const {
        Query q(db,"SELECT document FROM ontology_properties WHERE key=?"); q.bind(1,key);
        return q.row()?std::optional(decodeProperty(q.text(0))):std::nullopt;
    }
    void resourceExists(const OntologyResource &r, bool live) const {
        const auto key = nodeKey(r);
        if (r.kind == OntologyResourceKind::File) {
            Query q(db,std::string("SELECT deleted FROM objects WHERE object_key=?")+(live?" AND deleted=0":"")); q.bind(1,r.key);
            if (!q.row()) fail("ontology file is missing or deleted");
        } else {
            Query q(db,"SELECT 1 FROM ontology_nodes WHERE key=?"); q.bind(1,key); if (!q.row()) fail("ontology entity not found");
        }
    }
    void ensureResource(const OntologyResource &r) {
        resourceExists(r,true);
        if (r.kind == OntologyResourceKind::File) {
            Query q(db,"INSERT OR IGNORE INTO ontology_nodes(key,object_key,document) VALUES(?,?,'')");
            q.bind(1,nodeKey(r)); q.bind(2,r.key); q.done();
        }
    }
    bool hasType(const std::string &resource, const std::string &classKey) const {
        if (classKey.empty()) return true;
        Query q(db,R"SQL(WITH RECURSIVE lineage(key) AS (
 SELECT class_key FROM ontology_types WHERE resource_key=?
 UNION SELECT p.parent FROM ontology_parents p JOIN lineage a ON p.child=a.key)
 SELECT 1 FROM lineage WHERE key=? LIMIT 1)SQL");
        q.bind(1,resource); q.bind(2,classKey); return q.row();
    }
    void queryValid(const OntologyQuery &query) const {
        (void)queryJson(query); classExists(query.classKey);
        for (const auto &c : query.conditions) {
            const auto p = getProperty(c.propertyKey); if (!p) fail("ontology query property not found");
            if (c.equals && c.equals->kind()!=p->kind) fail("ontology query value type mismatch");
            if (c.equals && c.equals->kind()==OntologyValueKind::Resource) resourceExists(std::get<OntologyResource>(c.equals->data),false);
        }
    }
    void validateStatements(bool all) const {
        // Ordinary edits touch only indexed incident statements. Hierarchy edits
        // can affect every inherited domain/range and intentionally revalidate all.
        std::string sql = "SELECT s.subject,s.predicate,s.kind,s.value,s.object_key FROM ontology_statements s ";
        if (!all) sql += "WHERE s.subject IN(SELECT key FROM temp.ontology_changed_nodes) OR s.object_key IN(SELECT key FROM temp.ontology_changed_nodes) OR s.predicate IN(SELECT key FROM temp.ontology_changed_properties)";
        Query q(db,sql); std::set<std::pair<std::string,std::string>> singles;
        std::map<std::string,std::optional<OntologyProperty>> properties;
        std::map<std::pair<std::string,std::string>,bool> typeChecks;
        const auto typed=[&](const std::string &resource,const std::string &type) {
            if(type.empty())return true;
            auto [entry,inserted]=typeChecks.try_emplace({resource,type},false);
            if(inserted)entry->second=hasType(resource,type);
            return entry->second;
        };
        while (q.row()) {
            const auto subject=q.text(0), predicate=q.text(1);
            auto [definition,inserted]=properties.try_emplace(predicate);
            if(inserted)definition->second=getProperty(predicate);
            const auto &p=definition->second;
            if (!p || static_cast<int>(p->kind)!=q.number(2) || !typed(subject,p->domainClass)) fail("ontology property domain or value type violation");
            const auto v=decodeValue(static_cast<int>(q.number(2)),q.text(3));
            if (v.kind()==OntologyValueKind::Resource && !typed(nodeKey(std::get<OntologyResource>(v.data)),p->rangeClass)) fail("ontology property range violation");
            if (p->singleValue && singles.insert({subject,predicate}).second) {
                Query count(db,"SELECT count(*) FROM ontology_statements WHERE subject=? AND predicate=?"); count.bind(1,subject); count.bind(2,predicate);
                if (!count.row() || count.number(0)>1) fail("ontology single-value property violation");
            }
            if (p->acyclic) {
                Query cycle(db,R"SQL(WITH RECURSIVE reach(key) AS (SELECT ? UNION SELECT s.object_key FROM ontology_statements s JOIN reach r ON s.subject=r.key WHERE s.predicate=? AND s.object_key IS NOT NULL) SELECT 1 FROM reach WHERE key=? LIMIT 1)SQL");
                cycle.bind(1,nodeKey(std::get<OntologyResource>(v.data))); cycle.bind(2,predicate); cycle.bind(3,subject);
                if (cycle.row()) fail("cyclic ontology relation");
            }
        }
    }
    OntologyPage files(const OntologyQuery &query, const OntologyCursor &cursor, std::size_t limit) const {
        pageLimit(limit); queryValid(query);
        OntologyPage result; result.revision=revision(); result.fileSequence=scalar(db,"SELECT coalesce(max(sequence),0) FROM object_journal");
        detail::ObjectHash hash; hash.update(std::to_string(container.size()));hash.update(":");hash.update(container);hash.update(queryJson(query).dump());const auto signature=hash.finish();
        if (cursor.afterIndexKey<0 || cursor.fileSequence<0) fail("invalid ontology cursor");
        const bool initial=cursor.afterIndexKey==0 && cursor.revision==0 && cursor.fileSequence==0 && cursor.fingerprint.empty();
        if (!initial && (cursor.revision!=result.revision || cursor.fileSequence!=result.fileSequence || cursor.fingerprint!=signature)) fail("stale or mismatched ontology cursor");
        std::string sql;
        std::vector<std::variant<std::string,std::int64_t>> args;
        const auto bindText=[&](const std::string &s) { args.emplace_back(s); return std::string("?"); };
        if (!query.classKey.empty()) {
            sql="WITH RECURSIVE selected_class(key) AS (SELECT "+bindText(query.classKey);
            if (query.includeSubclasses) sql+=" UNION SELECT p.child FROM ontology_parents p JOIN selected_class c ON p.parent=c.key";
            sql+=") ";
        }
        sql+="SELECT i.object_key,i.index_key,i.head,i.path,i.sha256,i.size,i.validation_key,i.session_key,i.deleted,i.author_origin,i.author_subject FROM current_index i WHERE i.index_key>? ";
        args.emplace_back(cursor.afterIndexKey);
        if (!query.includeDeleted) sql+="AND i.deleted=0 ";
        if (!query.classKey.empty()) sql+="AND i.object_key IN(SELECT n.object_key FROM ontology_types t JOIN ontology_nodes n ON n.key=t.resource_key WHERE t.class_key IN(SELECT key FROM selected_class)) ";
        for (std::size_t i=0;i<query.conditions.size();++i) {
            const auto &c=query.conditions[i];
            // Indexed candidate sets avoid a scan of unrelated file contents or
            // annotation documents even when semantic filters are selective.
            sql+="AND i.object_key IN(SELECT n.object_key FROM ontology_statements s JOIN ontology_nodes n ON n.key=s.subject WHERE s.predicate="+bindText(c.propertyKey);
            if (c.equals) { sql+=" AND s.kind=? AND s.value="; args.emplace_back(static_cast<std::int64_t>(c.equals->kind())); sql+=bindText(valueJson(*c.equals).dump()); }
            sql+=") ";
        }
        sql+="ORDER BY i.index_key LIMIT ?"; args.emplace_back(static_cast<std::int64_t>(limit+1));
        Query q(db,sql); int at=1;
        for (const auto &arg:args) std::visit([&](const auto &v) { q.bind(at++,v); },arg);
        while(q.row()) result.files.push_back(indexEntry(q));
        if(result.files.size()>limit) {
            result.files.pop_back(); result.next=OntologyCursor{result.revision,result.fileSequence,result.files.back().indexKey,signature};
        }
        return result;
    }
};

OntologyFileSystem::OntologyFileSystem(const ObjectStore &store):m_impl(std::make_unique<Impl>()) {
    m_impl->directory=store.directory(); m_impl->container=store.containerKey(); m_impl->readOnly=store.access()==ObjectStore::Access::ReadOnly;
    const auto path=m_impl->directory/"objects.sqlite3";
    if (fs::canonical(m_impl->directory)!=m_impl->directory || fs::is_symlink(fs::symlink_status(path))) fail("redirected ontology package");
    for(const char *suffix:{"-wal","-shm","-journal"}) {
        auto sidecar=path;sidecar+=suffix;if(fs::is_symlink(fs::symlink_status(sidecar)))fail("redirected ontology sidecar");
    }
    int flags=(m_impl->readOnly?SQLITE_OPEN_READONLY:SQLITE_OPEN_READWRITE)|SQLITE_OPEN_FULLMUTEX;
#ifdef SQLITE_OPEN_NOFOLLOW
    flags|=SQLITE_OPEN_NOFOLLOW;
#endif
    const auto u=path.u8string(); check(m_impl->db,sqlite3_open_v2(reinterpret_cast<const char *>(u.c_str()),&m_impl->db,flags,nullptr));
    auto *db=m_impl->db; sqlite3_busy_timeout(db,5000);
    if(scalar(db,"PRAGMA application_id")!=0x534F424A || scalar(db,"PRAGMA user_version")!=5) fail("ontology requires schema 5; migrate with an ObjectStore writer first");
    execute(db,"PRAGMA foreign_keys=ON; PRAGMA trusted_schema=OFF;");
    Query identity(db,"SELECT container_key FROM store");if(!identity.row() || identity.text(0)!=m_impl->container || identity.row())fail("ontology container mismatch");
    if(m_impl->readOnly) execute(db,"PRAGMA query_only=ON;");
    else execute(db,R"SQL(PRAGMA synchronous=FULL; PRAGMA fullfsync=ON;
 CREATE TEMP TABLE ontology_changed_nodes(key TEXT PRIMARY KEY);
 CREATE TEMP TABLE ontology_changed_properties(key TEXT PRIMARY KEY);)SQL");
}
OntologyFileSystem::~OntologyFileSystem()=default;
std::uint64_t OntologyFileSystem::revision() const {std::lock_guard lock(m_impl->mutex);return m_impl->revision();}

std::uint64_t OntologyFileSystem::apply(const std::vector<OntologyChange> &changes,std::uint64_t expected,const std::string &session,std::stop_token cancellation) {
    if(cancellation.stop_requested())fail("ontology operation cancelled");
    if(changes.empty() || changes.size()>maximumBatchChanges)fail("ontology batch must contain 1 to 256 changes");
    text(session);std::lock_guard lock(m_impl->mutex);if(m_impl->readOnly)fail("read-only ontology");
    Transaction tx(m_impl->db); Progress progress(m_impl->db,cancellation); const auto previous=m_impl->revision();
    if(expected!=previous || expected>=INT64_MAX)fail("ontology revision conflict");
    {Query q(m_impl->db,"SELECT ended FROM sessions WHERE session_key=?");q.bind(1,session);if(!q.row() || q.number(0)!=0)fail("ontology requires an active work session");}
    execute(m_impl->db,"DELETE FROM temp.ontology_changed_nodes; DELETE FROM temp.ontology_changed_properties;");
    bool hierarchyChanged=false;std::size_t journalBytes=0,events=0;const auto next=previous+1;
    const auto changedNode=[&](const std::string &key){Query q(m_impl->db,"INSERT OR IGNORE INTO temp.ontology_changed_nodes VALUES(?)");q.bind(1,key);q.done();};
    const auto event=[&](const std::string &operation,const std::string &subject,const std::string &before,const std::string &after) {
        if(before==after)return;
        journalBytes+=before.size()+after.size();if(journalBytes>maximumBatchJournalBytes)fail("ontology batch journal exceeds 4 MiB");
        Query q(m_impl->db,"INSERT INTO ontology_journal(revision,operation,subject,before_json,after_json) VALUES(?,?,?,?,?)");
        q.bind(1,static_cast<std::int64_t>(next));q.bind(2,operation);q.bind(3,subject);q.bind(4,before);q.bind(5,after);q.done();++events;
    };
    for(const auto &change:changes) {
        progress.check();
        std::visit([&](const auto &c) {
            using T=std::decay_t<decltype(c)>;
            if constexpr(std::is_same_v<T,OntologyClass>) {
                const auto after=classJson(c);const auto old=m_impl->getClass(c.key);
                const auto before=old?classJson(*old).dump():std::string{};if(before==after.dump())return;
                for(const auto &p:c.parents)m_impl->classExists(p);
                Query q(m_impl->db,"INSERT INTO ontology_classes VALUES(?,?,?) ON CONFLICT(key) DO UPDATE SET label=excluded.label,description=excluded.description");
                q.bind(1,c.key);q.bind(2,c.label);q.bind(3,c.description);q.done();
                if(!old || classJson(*old)["parents"]!=after["parents"]) {
                    Query remove(m_impl->db,"DELETE FROM ontology_parents WHERE child=?");remove.bind(1,c.key);remove.done();
                    for(const auto &p:c.parents){Query add(m_impl->db,"INSERT INTO ontology_parents VALUES(?,?)");add.bind(1,c.key);add.bind(2,p);add.done();}
                    Query cycle(m_impl->db,"WITH RECURSIVE ancestors(key) AS (SELECT parent FROM ontology_parents WHERE child=? UNION SELECT p.parent FROM ontology_parents p JOIN ancestors a ON p.child=a.key) SELECT 1 FROM ancestors WHERE key=? LIMIT 1");
                    cycle.bind(1,c.key);cycle.bind(2,c.key);if(cycle.row())fail("cyclic ontology class hierarchy");hierarchyChanged=hierarchyChanged || old.has_value();
                }
                event("class",c.key,before,after.dump());
            } else if constexpr(std::is_same_v<T,OntologyProperty>) {
                const auto after=propertyJson(c).dump();m_impl->classExists(c.domainClass);m_impl->classExists(c.rangeClass);
                const auto old=m_impl->getProperty(c.key);const auto before=old?propertyJson(*old).dump():std::string{};
                if(before==after)return;
                Query q(m_impl->db,"INSERT INTO ontology_properties VALUES(?,?) ON CONFLICT(key) DO UPDATE SET document=excluded.document");q.bind(1,c.key);q.bind(2,after);q.done();
                Query scope(m_impl->db,"INSERT OR IGNORE INTO temp.ontology_changed_properties VALUES(?)");scope.bind(1,c.key);scope.done();event("property",c.key,before,after);
            } else if constexpr(std::is_same_v<T,OntologyEntity>) {
                text(c.key);text(c.label,4096,false);text(c.description,16384,false);
                const auto key=nodeKey(OntologyResource::entity(c.key));const auto after=Json{{"key",c.key},{"label",c.label},{"description",c.description}}.dump();
                Query previous(m_impl->db,"SELECT document FROM ontology_nodes WHERE key=?");previous.bind(1,key);const auto before=previous.row()?previous.text(0):std::string{};
                if(before==after)return;
                Query q(m_impl->db,"INSERT INTO ontology_nodes(key,object_key,document) VALUES(?,NULL,?) ON CONFLICT(key) DO UPDATE SET document=excluded.document");q.bind(1,key);q.bind(2,after);q.done();event("entity",key,before,after);
            } else if constexpr(std::is_same_v<T,OntologyTypeAssignment>) {
                m_impl->classExists(c.classKey);text(c.classKey);
                if(c.present)m_impl->ensureResource(c.resource);else m_impl->resourceExists(c.resource,false);
                const auto key=nodeKey(c.resource);
                Query q(m_impl->db,c.present?"INSERT OR IGNORE INTO ontology_types VALUES(?,?)":"DELETE FROM ontology_types WHERE resource_key=? AND class_key=?");q.bind(1,key);q.bind(2,c.classKey);q.done();
                if(sqlite3_changes(m_impl->db)){const auto value=Json{{"class",c.classKey}}.dump();event("type",key,c.present?"":value,c.present?value:"");changedNode(key);}
            } else if constexpr(std::is_same_v<T,OntologyAssertion>) {
                text(c.propertyKey);const auto p=m_impl->getProperty(c.propertyKey);if(!p || (c.present && p->kind!=c.value.kind()))fail("ontology assertion type mismatch");
                if(c.present)m_impl->ensureResource(c.subject);else m_impl->resourceExists(c.subject,false);
                const auto subject=nodeKey(c.subject);const auto value=valueJson(c.value).dump();
                std::string object;
                if(c.value.kind()==OntologyValueKind::Resource){const auto &target=std::get<OntologyResource>(c.value.data);if(c.present)m_impl->ensureResource(target);else m_impl->resourceExists(target,false);object=nodeKey(target);}
                if(c.present){Query q(m_impl->db,"INSERT OR IGNORE INTO ontology_statements(subject,predicate,kind,value,object_key) VALUES(?,?,?,?,nullif(?,''))");q.bind(1,subject);q.bind(2,c.propertyKey);q.bind(3,static_cast<std::int64_t>(c.value.kind()));q.bind(4,value);q.bind(5,object);q.done();}
                else {Query q(m_impl->db,"DELETE FROM ontology_statements WHERE subject=? AND predicate=? AND kind=? AND value=?");q.bind(1,subject);q.bind(2,c.propertyKey);q.bind(3,static_cast<std::int64_t>(c.value.kind()));q.bind(4,value);q.done();}
                if(sqlite3_changes(m_impl->db)){const auto document=Json{{"property",c.propertyKey},{"kind",static_cast<int>(c.value.kind())},{"value",valueJson(c.value)}}.dump();event("statement",subject,c.present?"":document,c.present?document:"");changedNode(subject);if(!object.empty())changedNode(object);}
            } else if constexpr(std::is_same_v<T,OntologyDirectory>) {
                directoryPath(c.path);text(c.label,4096,false);m_impl->queryValid(c.query);
                const auto after=Json{{"label",c.label},{"query",queryJson(c.query)}}.dump();
                Query previous(m_impl->db,"SELECT document FROM ontology_directories WHERE path=?");previous.bind(1,c.path);const auto before=previous.row()?previous.text(0):std::string{};
                if(before==after)return;
                Query q(m_impl->db,"INSERT INTO ontology_directories VALUES(?,?) ON CONFLICT(path) DO UPDATE SET document=excluded.document");q.bind(1,c.path);q.bind(2,after);q.done();event("directory",c.path,before,after);
                Query remove(m_impl->db,"DELETE FROM ontology_directory_properties WHERE path=?");remove.bind(1,c.path);remove.done();
                for(const auto &condition:c.query.conditions){Query add(m_impl->db,"INSERT OR IGNORE INTO ontology_directory_properties VALUES(?,?)");add.bind(1,c.path);add.bind(2,condition.propertyKey);add.done();}
            } else if constexpr(std::is_same_v<T,OntologyRemoveDirectory>) {
                directoryPath(c.path);Query previous(m_impl->db,"SELECT document FROM ontology_directories WHERE path=?");previous.bind(1,c.path);if(!previous.row())return;const auto before=previous.text(0);
                Query q(m_impl->db,"DELETE FROM ontology_directories WHERE path=?");q.bind(1,c.path);q.done();event("directory",c.path,before,"");
            }
        },change);
    }
    if(!events){progress.check();tx.commit();return previous;}
    m_impl->validateStatements(hierarchyChanged);
    {
        Query affected(m_impl->db,"SELECT document FROM ontology_directories WHERE path IN(SELECT path FROM ontology_directory_properties WHERE predicate IN(SELECT key FROM temp.ontology_changed_properties))");
        while(affected.row())m_impl->queryValid(decodeQuery(Json::parse(affected.text(0)).at("query")));
    }
    {Query q(m_impl->db,"INSERT INTO ontology_commits VALUES(?,?,?)");q.bind(1,static_cast<std::int64_t>(next));q.bind(2,session);q.bind(3,now());q.done();}
    {Query q(m_impl->db,"UPDATE ontology_state SET revision=? WHERE id=1");q.bind(1,static_cast<std::int64_t>(next));q.done();}
    progress.check();tx.commit();return next;
}

std::optional<OntologyClass> OntologyFileSystem::ontologyClass(const std::string &key) const {
    text(key);std::lock_guard lock(m_impl->mutex);Transaction tx(m_impl->db,false);auto result=m_impl->getClass(key);tx.commit();return result;
}
std::vector<OntologyClass> OntologyFileSystem::classes(const std::string &after,std::size_t limit) const {
    text(after,1024,false);pageLimit(limit);std::lock_guard lock(m_impl->mutex);Transaction tx(m_impl->db,false);
    Query q(m_impl->db,"WITH page AS(SELECT key,label,description FROM ontology_classes WHERE key>? ORDER BY key LIMIT ?) SELECT c.key,c.label,c.description,p.parent FROM page c LEFT JOIN ontology_parents p ON p.child=c.key ORDER BY c.key,p.parent");q.bind(1,after);q.bind(2,static_cast<std::int64_t>(limit));
    std::vector<OntologyClass> result;while(q.row()){if(result.empty() || result.back().key!=q.text(0))result.push_back({q.text(0),q.text(1),q.text(2),{}});if(sqlite3_column_type(q.stmt,3)!=SQLITE_NULL)result.back().parents.push_back(q.text(3));}tx.commit();return result;
}
std::optional<OntologyProperty> OntologyFileSystem::property(const std::string &key) const {text(key);std::lock_guard lock(m_impl->mutex);return m_impl->getProperty(key);}
std::optional<OntologyEntity> OntologyFileSystem::entity(const std::string &key) const {
    text(key);std::lock_guard lock(m_impl->mutex);Query q(m_impl->db,"SELECT document FROM ontology_nodes WHERE key=?");q.bind(1,nodeKey(OntologyResource::entity(key)));if(!q.row())return {};
    auto j=Json::parse(q.text(0));return OntologyEntity{key,j.at("label").get<std::string>(),j.at("description").get<std::string>()};
}
std::vector<OntologyStatement> OntologyFileSystem::statements(const OntologyResource &subject,std::int64_t after,std::size_t limit) const {
    const auto key=nodeKey(subject);if(after<0)fail("invalid ontology statement cursor");pageLimit(limit);std::lock_guard lock(m_impl->mutex);
    Query q(m_impl->db,"SELECT sequence,predicate,kind,value FROM ontology_statements WHERE subject=? AND sequence>? ORDER BY sequence LIMIT ?");q.bind(1,key);q.bind(2,after);q.bind(3,static_cast<std::int64_t>(limit));
    std::vector<OntologyStatement> result;while(q.row())result.push_back({q.number(0),subject,q.text(1),decodeValue(static_cast<int>(q.number(2)),q.text(3))});return result;
}
OntologyPage OntologyFileSystem::queryFiles(const OntologyQuery &query,const OntologyCursor &cursor,std::size_t limit,std::stop_token cancellation) const {
    std::lock_guard lock(m_impl->mutex);Transaction tx(m_impl->db,false);Progress progress(m_impl->db,cancellation);
    auto result=m_impl->files(query,cursor,limit);progress.check();tx.commit();return result;
}
OntologyPage OntologyFileSystem::listDirectory(const std::string &path,const OntologyCursor &cursor,std::size_t limit,std::stop_token cancellation) const {
    directoryPath(path);std::lock_guard lock(m_impl->mutex);Transaction tx(m_impl->db,false);Progress progress(m_impl->db,cancellation);
    Query q(m_impl->db,"SELECT document FROM ontology_directories WHERE path=?");q.bind(1,path);if(!q.row())fail("ontology directory not found");
    auto result=m_impl->files(decodeQuery(Json::parse(q.text(0)).at("query")),cursor,limit);progress.check();tx.commit();return result;
}
std::vector<OntologyDirectory> OntologyFileSystem::directories(const std::string &prefix,const std::string &after,std::size_t limit) const {
    text(prefix,4096,false);text(after,4096,false);pageLimit(limit);std::lock_guard lock(m_impl->mutex);
    Query q(m_impl->db,"SELECT path,document FROM ontology_directories WHERE path>? AND path>=? AND substr(path,1,length(?))=? ORDER BY path LIMIT ?");
    q.bind(1,after);q.bind(2,prefix);q.bind(3,prefix);q.bind(4,prefix);q.bind(5,static_cast<std::int64_t>(limit));
    std::vector<OntologyDirectory> result;while(q.row()){auto j=Json::parse(q.text(1));result.push_back({q.text(0),j.at("label").get<std::string>(),decodeQuery(j.at("query"))});}return result;
}
OntologyWalk OntologyFileSystem::traverse(const OntologyResource &start,const std::string &predicate,OntologyDirection direction,unsigned depth,std::size_t limit,bool deleted,std::stop_token cancellation) const {
    const auto key=nodeKey(start);text(predicate);pageLimit(limit);if(!depth || depth>32 || (direction!=OntologyDirection::Outgoing && direction!=OntologyDirection::Incoming))fail("invalid ontology traversal bounds");
    std::lock_guard lock(m_impl->mutex);Transaction tx(m_impl->db,false);Progress progress(m_impl->db,cancellation);m_impl->resourceExists(start,!deleted);
    const auto p=m_impl->getProperty(predicate);if(!p || p->kind!=OntologyValueKind::Resource)fail("ontology traversal requires a resource property");
    const bool forward=direction==OntologyDirection::Outgoing;
    const std::string target=forward?"s.object_key":"s.subject",source=forward?"s.subject":"s.object_key";
    std::string sql="WITH RECURSIVE reach(key,depth) AS(SELECT ?,0 UNION SELECT "+target+",r.depth+1 FROM reach r JOIN ontology_statements s ON "+source+"=r.key JOIN ontology_nodes n ON n.key="+target+" LEFT JOIN objects o ON o.object_key=n.object_key WHERE s.predicate=? AND s.kind=0 AND r.depth<? ";
    if(!deleted)sql+="AND (n.object_key IS NULL OR o.deleted=0) ";
    sql+="ORDER BY 2 ASC LIMIT ?) SELECT key,min(depth), (SELECT count(*) FROM reach) FROM reach WHERE key<>? GROUP BY key ORDER BY min(depth),key LIMIT ?";
    Query q(m_impl->db,sql);q.bind(1,key);q.bind(2,predicate);q.bind(3,static_cast<std::int64_t>(depth));q.bind(4,static_cast<std::int64_t>(maximumTraversalRows+1));q.bind(5,key);q.bind(6,static_cast<std::int64_t>(limit+1));
    OntologyWalk result;while(q.row()){result.nodes.push_back({resourceKey(q.text(0)),static_cast<unsigned>(q.number(1))});if(q.number(2)>static_cast<std::int64_t>(maximumTraversalRows))result.truncated=true;}
    if(result.nodes.size()>limit){result.nodes.pop_back();result.truncated=true;}progress.check();tx.commit();return result;
}
std::vector<OntologyEvent> OntologyFileSystem::journal(std::int64_t after,std::size_t limit,const std::string &session) const {
    if(after<0)fail("invalid ontology journal cursor");pageLimit(limit);text(session,1024,false);std::lock_guard lock(m_impl->mutex);
    const auto sql=std::string("SELECT j.sequence,j.revision,j.operation,j.subject,j.before_json,j.after_json,c.session_key,c.at_ns FROM ontology_journal j JOIN ontology_commits c ON c.revision=j.revision WHERE j.sequence>? ")+(session.empty()?"":"AND c.session_key=? ")+"ORDER BY j.sequence LIMIT ?";
    Query q(m_impl->db,sql);q.bind(1,after);int at=2;if(!session.empty())q.bind(at++,session);q.bind(at,static_cast<std::int64_t>(limit));
    std::vector<OntologyEvent> result;std::size_t bytes=0;while(q.row()){const auto before=q.text(4),after=q.text(5);if(bytes+before.size()+after.size()>8*1024*1024)break;bytes+=before.size()+after.size();result.push_back({q.number(0),static_cast<std::uint64_t>(q.number(1)),q.text(2),q.text(3),before,after,q.text(6),q.number(7)});}return result;
}
void OntologyFileSystem::extractFile(const std::string &key,std::uint64_t version,const fs::path &destination) const {
    ObjectStore reader(m_impl->directory,m_impl->container,ObjectStore::Access::ReadOnly);reader.extract(key,version,destination);
}
} // namespace iiFileProvider
