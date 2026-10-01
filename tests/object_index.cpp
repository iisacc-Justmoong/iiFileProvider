#include "ObjectStore.h"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace iiFileProvider;
namespace fs=std::filesystem;
void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
struct Database {
    sqlite3 *db=nullptr;
    explicit Database(const fs::path &path){const auto utf8=path.u8string();require(sqlite3_open(reinterpret_cast<const char *>(utf8.c_str()),&db)==SQLITE_OK,"open fixture database");}
    ~Database(){sqlite3_close(db);}
    void exec(const char *sql){char *error=nullptr;const int status=sqlite3_exec(db,sql,nullptr,nullptr,&error);const std::string detail=error?error:"";sqlite3_free(error);if(status!=SQLITE_OK)throw std::runtime_error(detail);}
    std::int64_t scalar(const char *sql){sqlite3_stmt *s=nullptr;require(sqlite3_prepare_v2(db,sql,-1,&s,nullptr)==SQLITE_OK,"prepare scalar");const int result=sqlite3_step(s);const auto value=sqlite3_column_int64(s,0);sqlite3_finalize(s);require(result==SQLITE_ROW,"scalar row");return value;}
};
void matches(ObjectStore &store){
    const auto full=store.scan(0,10000,true);
    const auto index=store.index(0,10000,true);
    require(full.size()==index.size(),"projection count agrees with authoritative heads");
    for(std::size_t i=0;i<full.size();++i){const auto &r=full[i];const auto &e=index[i];
        require(e.key==r.key && e.indexKey==r.indexKey && e.version==r.version && e.path==r.path
            && e.sha256==r.sha256 && e.size==r.size && e.validationKey==r.validationKey
            && e.sessionKey==r.sessionKey && e.deleted==r.deleted
            && e.authorOrigin==r.author.origin && e.authorSubject==r.author.subject,"all compact fields match");
    }
    std::size_t live=0;std::int64_t cursor=0;
    for(;;){const auto page=store.index(cursor,7);if(page.empty())break;
        for(const auto &entry:page){require(!entry.deleted && entry.indexKey>cursor,"ordered live keyset page");cursor=entry.indexKey;++live;}}
    require(live==store.count(),"live pagination complete");
}
int main(){try{
    const auto root=fs::path(OBJECT_TEST_DIRECTORY)/("object-index-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);const auto source=root/"source";
    {std::ofstream out(source);out<<"compact projection payload";}
    const auto package=root/"package";const auto database=package/"objects.sqlite3";
    std::vector<ObjectRecord> imported;std::string session;
    {
        ObjectStore store(package,"index-fixture",true);
        {Database db(database);require(db.scalar("PRAGMA user_version")==3,"compact index requires schema 3");}
        session=store.beginSession({"local:test","index-fixture","Index test"},"fixture","compact traversal").key;
        std::vector<ObjectImport> input;
        for(int i=0;i<64;++i)input.push_back({source,"Files/"+std::to_string(i),{}});
        imported=store.importFiles(input,session);matches(store);
        store.move(imported[0].key,"Files/moved",1,session);matches(store);
        store.reviseFile(imported[1].key,source,1,session);matches(store);
        store.erase(imported[2].key,1,session);matches(store);
        bool failed=false;
        try{store.importFiles({{source,"Files/rollback",{}},{source,"Files/moved",{}}},session);}catch(const std::exception &){failed=true;}
        require(failed && !store.lookupPath("Files/rollback"),"failed batch rolls back projection and objects");matches(store);
    }
    {
        Database db(database);
        require(db.scalar("SELECT count(*) FROM current_index")==64,"persistent compact row for each object");
        // The query plan must need only the compact ordered table: no history,
        // payload, session lookup or temporary ordering structure.
        sqlite3_stmt *plan=nullptr;
        require(sqlite3_prepare_v2(db.db,"EXPLAIN QUERY PLAN SELECT object_key,index_key,head,path,sha256,size,validation_key,session_key,deleted,author_origin,author_subject FROM current_index WHERE index_key>? AND deleted=0 ORDER BY index_key LIMIT ?",-1,&plan,nullptr)==SQLITE_OK,"index plan");
        int steps=0;while(sqlite3_step(plan)==SQLITE_ROW){const std::string detail=reinterpret_cast<const char *>(sqlite3_column_text(plan,3));
            require(detail.find("SEARCH current_index USING INTEGER PRIMARY KEY")!=std::string::npos,"single ordered compact-table range search");++steps;}
        sqlite3_finalize(plan);require(steps==1,"one index range, no joins or temp sort");
        // Produce the exact previous schema from this fixture; payload/history
        // remain untouched. Reopening must transactionally backfill every head.
        db.exec("DROP TRIGGER current_index_publish; DROP TABLE current_index; PRAGMA user_version=2;");
    }
    bool wrong=false;try{ObjectStore store(package,"wrong-container");}catch(const std::exception &){wrong=true;}
    require(wrong,"wrong container rejected before migration");
    {
        Database db(database);require(db.scalar("PRAGMA user_version")==2,"wrong container cannot upgrade");
        // Force failure after table creation/backfill, not before migration starts.
        db.exec("CREATE TRIGGER current_index_publish AFTER UPDATE ON objects BEGIN SELECT 1; END;");
    }
    bool migrationFailed=false;try{ObjectStore store(package,"index-fixture");}catch(const std::exception &){migrationFailed=true;}
    require(migrationFailed,"failed migration rejected");
    {
        Database db(database);
        require(db.scalar("PRAGMA user_version")==2 && db.scalar("SELECT count(*) FROM sqlite_master WHERE type='table' AND name='current_index'")==0,"failed migration rolls back schema and backfill");
        db.exec("DROP TRIGGER current_index_publish;");
    }
    {
        ObjectStore store(package,"index-fixture");matches(store);
        {Database db(database);require(db.scalar("PRAGMA user_version")==3,"schema 2 upgraded");}
        require(store.lookup(imported[3].key)->validationKey==imported[3].validationKey,"migration preserves validation identity");
        require(store.validate(imported[0].key,2) && store.validate(imported[2].key,2),"history and payload remain valid after backfill");
        store.move(imported[3].key,"Files/after-upgrade",1,session);matches(store);
        store.endSession(session);
    }
    {ObjectStore store(package,"index-fixture");matches(store);}
    fs::remove_all(root);std::cout<<"Compact persistent index, atomic updates, schema-2 upgrade and keyset traversal passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
