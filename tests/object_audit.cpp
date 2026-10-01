#include "ObjectPackager.h"
#include <sqlite3.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs=std::filesystem;
using namespace iiFileProvider;
void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
void write(const fs::path &path,const std::string &data){std::ofstream f(path,std::ios::binary);f<<data;check(bool(f),"fixture write");}
void sql(const fs::path &path,const std::string &statement){
    sqlite3 *db=nullptr;const auto text=path.u8string();
    check(sqlite3_open(reinterpret_cast<const char *>(text.c_str()),&db)==SQLITE_OK,"fixture SQLite open");
    const auto status=sqlite3_exec(db,statement.c_str(),nullptr,nullptr,nullptr);sqlite3_close(db);check(status==SQLITE_OK,"fixture SQLite mutation");
}
int main(){try{
    const auto root=fs::path(OBJECT_TEST_DIRECTORY)/("object-audit-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);fs::create_directory(root/"tree");
    for(unsigned i=0;i<12;++i)write(root/"tree"/std::to_string(i),i?"payload-"+std::to_string(i):"");
    std::string large(17*1024*1024+17,'z');large[1024*1024-1]='X';large[1024*1024]='Y';large.back()='Z';
    write(root/"tree/large",large);large.clear();large.shrink_to_fit();
    const std::vector<ObjectTreeMapping> mappings{{"Files",root/"tree",{}}};
    auto inventory=ObjectPackager::inventory(mappings);
    {
        ObjectStore store(root/"package","audit-fixture",true);ObjectPackager packager(store);
        const auto session=store.beginSession({"local:test","audit","Audit fixture"},"fixture","audit");
        const auto packaged=packager.package(inventory,session.key,{}, {}, {},ObjectStore::maximumBatchFiles);
        check(packaged.imported==13 && packaged.issues.empty(),"audit fixture packaged");
        std::size_t callbacks=0;
        auto report=packager.audit(inventory,{},[&](const auto &,const std::string &key){check(!key.empty(),"verified key");++callbacks;});
        check(report.verified==13 && report.issues.empty() && !report.cancelled && callbacks==13,"batch and large streaming audit");
        check(fs::is_empty(root/"package/staging"),"audit snapshots cleaned");
        std::stop_source stop;
        report=packager.audit(inventory,stop.get_token(),[&](const auto &,const auto &){stop.request_stop();});
        check(report.cancelled && report.verified==1 && report.issues.empty(),"audit cancellation preserves verified prefix");
        check(fs::is_empty(root/"package/staging"),"cancelled audit snapshots cleaned");
        const auto moving=store.lookupPath("Files/1").value();bool moved=false;
        report=packager.audit(inventory,{},[&](const auto &entry,const auto &){
            if(entry.logicalPath=="Files/0" && !moved){store.move(moving.key,"Files/renamed",1,session.key);moved=true;}
        });
        check(moved && report.issues.size()==1 && report.verified==12,"changed head cannot pass a prefetched audit");
        store.move(moving.key,"Files/1",2,session.key);
        write(root/"tree/0","changed source");
        report=packager.audit(inventory);check(report.issues.size()==1 && report.verified==12,"changed source rejects only affected member");
        inventory=ObjectPackager::inventory(mappings);
        const auto repaired=packager.package(inventory,session.key);
        check(repaired.revised==1 && repaired.issues.empty(),"source reconciled before audit");
        report=packager.audit(inventory);check(report.verified==13 && report.issues.empty(),"reconciled source and moved-back head validate");
        const auto extra=store.importFile(root/"tree/1","Unmapped/extra",session.key);
        report=packager.audit(inventory);check(report.verified==13 && report.issues.size()==1,"unmapped extra object detected");
        store.erase(extra.key,1,session.key);
        const auto first=store.lookupPath("Files/0").value();const auto database=root/"package/objects.sqlite3";
        sql(database,"UPDATE current_index SET sha256='corrupt' WHERE object_key='"+first.key+"'");
        report=packager.audit(inventory);check(report.verified==12 && report.issues.size()==1,"compact projection corruption detected");
        sql(database,"UPDATE objects SET head=head WHERE object_key='"+first.key+"'");
        sql(database,"UPDATE chunks SET data=zeroblob(length(data)) WHERE sha256='"+first.sha256+"'");
        report=packager.audit(inventory);check(report.verified==12 && report.issues.size()==1,"stored payload corruption detected");
        check(fs::is_empty(root/"package/staging") && store.count()==13,"audit does not mutate objects or retain staging");
        store.endSession(session.key);
    }
    fs::remove_all(root);std::cout<<"Bounded audit, large streaming, cancellation, head/source changes and index/payload corruption passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
