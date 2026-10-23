#include "OntologyFileSystem.h"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>

using namespace iiFileProvider;
namespace fs=std::filesystem;
void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
template<class F> void rejects(F f){bool rejected=false;try{f();}catch(const std::exception &){rejected=true;}require(rejected,"operation must reject");}
struct Database {
    sqlite3 *db=nullptr;
    explicit Database(const fs::path &path){const auto u=path.u8string();require(sqlite3_open(reinterpret_cast<const char *>(u.c_str()),&db)==SQLITE_OK,"fixture open");}
    ~Database(){sqlite3_close(db);}
    void exec(const char *sql){char *error=nullptr;const auto code=sqlite3_exec(db,sql,nullptr,nullptr,&error);const std::string message=error?error:"";sqlite3_free(error);if(code!=SQLITE_OK)throw std::runtime_error(message);}
    std::int64_t scalar(const char *sql){sqlite3_stmt *s=nullptr;require(sqlite3_prepare_v2(db,sql,-1,&s,nullptr)==SQLITE_OK,"fixture prepare");const auto code=sqlite3_step(s);const auto value=sqlite3_column_int64(s,0);sqlite3_finalize(s);require(code==SQLITE_ROW,"fixture value");return value;}
};
int main() try {
    const auto root=fs::path(OBJECT_TEST_DIRECTORY)/("ontology-storage-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);{std::ofstream out(root/"source");out<<"immutable source";}
    ObjectRecord raw;std::string session;
    {
        ObjectStore store(root/"package","migration",true);session=store.beginSession({"local:test","editor","Editor"},"device","schema 4").key;
        RawMetadata metadata;metadata.originalName="source";raw=store.wrapRaw(root/"source","Files/raw",metadata,session);
        store.importFile(root/"source","Files/other",session);
    }
    const auto database=root/"package/objects.sqlite3";
    {
        Database db(database);
        db.exec("DROP TABLE ontology_directory_properties; DROP TABLE ontology_directories; DROP TABLE ontology_journal; DROP TABLE ontology_commits; DROP TABLE ontology_statements; DROP TABLE ontology_types; DROP TABLE ontology_nodes; DROP TABLE ontology_properties; DROP TABLE ontology_parents; DROP TABLE ontology_classes; DROP TABLE ontology_state; DROP INDEX ontology_file_object; PRAGMA user_version=4;");
    }
    {
        ObjectStore reader(root/"package","migration",ObjectStore::Access::ReadOnly);
        require(reader.validate(raw.key,1) && reader.journal().size()==2,"schema 4 RAW and journal stay readable");
        rejects([&]{OntologyFileSystem ontology(reader);});
        Database db(database);require(db.scalar("PRAGMA user_version")==4,"read-only ontology never migrates");
    }
    {
        Database db(database);db.exec("CREATE TABLE ontology_file_object(id INTEGER);");
    }
    rejects([&]{ObjectStore store(root/"package","migration");});
    {
        Database db(database);require(db.scalar("PRAGMA user_version")==4 && db.scalar("SELECT count(*) FROM sqlite_master WHERE name='ontology_state'")==0,"failed ontology migration rolls back all schema work");
        db.exec("DROP TABLE ontology_file_object;");
    }
    std::unique_ptr<OntologyFileSystem> retained;
    {
        ObjectStore writer(root/"package","migration");
        Database db(database);require(db.scalar("PRAGMA user_version")==5,"writer migrates to schema 5");
        require(writer.lookup(raw.key)->validationKey==raw.validationKey && writer.validate(raw.key,1),"schema upgrade preserves raw validation and payload");
        retained=std::make_unique<OntologyFileSystem>(writer);
        retained->apply({OntologyClass{"Asset","Asset","",{}},OntologyClass{"Photo","Photo","",{"Asset"}},
            OntologyProperty{"count","Count",OntologyValueKind::Integer,"Asset","",true},
            OntologyTypeAssignment{OntologyResource::file(raw.key),"Photo"}},0,session);
        require(writer.journal().size()==2 && writer.lookup(raw.key)->version==1,"semantic edits leave byte revision journal intact");
        OntologyFileSystem competitor(writer);const auto rival=writer.beginSession({"local:test","rival","Rival"},"other","CAS");
        auto attempt=[&](OntologyFileSystem &s,const std::string &id,const std::string &key){try{s.apply({OntologyEntity{key,key,""}},1,id);return true;}catch(const std::exception &){return false;}};
        auto a=std::async(std::launch::async,[&]{return attempt(*retained,session,"first");});
        auto b=std::async(std::launch::async,[&]{return attempt(competitor,rival.key,"second");});
        require(int(a.get())+int(b.get())==1 && retained->revision()==2,"one ontology CAS writer wins");
        const auto before=retained->journal().size();
        rejects([&]{retained->apply({OntologyEntity{"rollback","Rollback",""},OntologyAssertion{OntologyResource::entity("rollback"),"count",OntologyValue::integer(1)}},2,session);});
        require(!retained->entity("rollback") && retained->journal().size()==before && retained->revision()==2,"late domain failure rolls back entity, events and revision");
        ObjectStore readonly(root/"package","migration",ObjectStore::Access::ReadOnly);OntologyFileSystem reader(readonly);
        require(reader.queryFiles({"Asset"}).files.size()==1,"WAL reader sees committed semantic graph");
        rejects([&]{reader.apply({OntologyEntity{"forbidden","No",""}},2,session);});
        const auto page=retained->queryFiles({}, {},1);require(page.next.has_value(),"pagination fixture");
        retained->apply({OntologyEntity{"next","Next",""}},2,session);
        rejects([&]{retained->queryFiles({},*page.next);});
        const auto events=retained->journal(0,1024,session);
        for(const auto &event:events)require(event.sessionKey==session,"ontology session journal filter");
        const auto chunkCount=db.scalar("SELECT count(*) FROM chunks");
        retained->apply({OntologyAssertion{OntologyResource::file(raw.key),"count",OntologyValue::integer(10)}},3,session);
        require(db.scalar("SELECT count(*) FROM chunks")==chunkCount,"semantic writes never ingest payload chunks");
        rejects([&]{retained->apply({OntologyClass{"Photo","Photo","",{}}},4,session);});
        require(retained->ontologyClass("Photo")->parents==std::vector<std::string>{"Asset"},"hierarchy edits cannot invalidate existing domains");
        writer.endSession(rival.key);
    }
    require(retained->revision()==4 && retained->queryFiles({"Asset"}).files.size()==1,"facade survives originating ObjectStore lifetime");
    retained->extractFile(raw.key,1,root/"extracted");require(fs::file_size(root/"extracted")==fs::file_size(root/"source"),"retained facade can provide file bytes");
    retained.reset();
    {
        Database db(database);db.exec("VACUUM;");
    }
    {
        ObjectStore store(root/"package","migration");OntologyFileSystem ontology(store);const auto last=ontology.journal().back().sequence;
        ontology.apply({OntologyEntity{"after-vacuum","After vacuum",""}},4,session);
        require(ontology.journal(last).size()==1 && ontology.journal(last)[0].sequence>last,"ontology journal cursor survives vacuum and reopen");
        // The file lookup index and semantic candidate index must both remain available.
        Database db(database);require(db.scalar("SELECT count(*) FROM sqlite_master WHERE type='index' AND name IN('ontology_types_class','ontology_statement_value','ontology_file_object')")==3,"indexed semantic candidate and file lookup paths");
        sqlite3_stmt *plan=nullptr;
        require(sqlite3_prepare_v2(db.db,"EXPLAIN QUERY PLAN WITH RECURSIVE selected_class(key) AS(SELECT 'Asset' UNION SELECT p.child FROM ontology_parents p JOIN selected_class c ON p.parent=c.key) SELECT i.object_key FROM current_index i WHERE i.index_key>0 AND i.deleted=0 AND i.object_key IN(SELECT n.object_key FROM ontology_types t JOIN ontology_nodes n ON n.key=t.resource_key WHERE t.class_key IN(SELECT key FROM selected_class)) ORDER BY i.index_key LIMIT 256",-1,&plan,nullptr)==SQLITE_OK,"semantic query plan");
        std::string details;while(sqlite3_step(plan)==SQLITE_ROW)details+=reinterpret_cast<const char *>(sqlite3_column_text(plan,3));sqlite3_finalize(plan);
        require(details.find("ontology_types_class")!=std::string::npos && details.find("ontology_file_object")!=std::string::npos && details.find("raw_revisions")==std::string::npos && details.find("chunks")==std::string::npos,"semantic query uses indexed candidates without payload or RAW metadata scans");
        store.endSession(session);
    }
    fs::remove_all(root);std::cout<<"Ontology migration, rollback, CAS, WAL, capability and lifetime verified\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
