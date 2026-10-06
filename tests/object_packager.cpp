#include "native_symlink.h"
#include "ObjectPackager.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
namespace fs=std::filesystem;
using namespace iiFileProvider;
void check(bool good,const char *message){if(!good)throw std::runtime_error(message);}
void write(const fs::path &p,const std::string &data){std::ofstream file(p,std::ios::binary);file<<data;check(bool(file),"write fixture");}
std::string read(const fs::path &p){std::ifstream file(p,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception &){rejected=true;}check(rejected,"operation must reject");}
int main(){try{
    const auto root=fs::current_path()/("packager-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root/"tree/sub");fs::create_directory(root/"stage");
    write(root/"tree/sub/a.txt","before");
    const auto identity=ObjectSource::inspect(root/"tree/sub/a.txt");
    {
        ObjectSource snapshot(root/"tree/sub/a.txt",root/"stage");
        check(snapshot.identity()==identity && read(snapshot.path())=="before","source snapshot content");
        write(root/"tree/sub/a.txt","after");
        check(read(snapshot.path())=="before" && ObjectSource::inspect(root/"tree/sub/a.txt")!=identity,"snapshot independent of later source edit");
    }
    check(fs::is_empty(root/"stage"),"owned staging cleaned");
    test_support::create_symlink(root/"tree",root/"redirect", true);
    rejects([&]{ObjectSource::inspect(root/"redirect/sub/a.txt");});
    test_support::create_symlink(root/"tree/sub/a.txt",root/"redirect-file");
    rejects([&]{ObjectSource::inspect(root/"redirect-file");});
    write(root/"tree/b.txt","b");
    fs::create_directory(root/"tree/ignored");write(root/"tree/ignored/private","not included by explicit policy");
    const std::vector<ObjectTreeMapping> maps{{"Files",root/"tree",{"ignored"}}};
    auto inventory=ObjectPackager::inventory(maps);check(inventory.entries.size()==2 && inventory.totalBytes==6 && inventory.issues.empty(),"mapped inventory");
    const auto serial=ObjectPackager::inventory(maps,{},1);
    check(serial.entries.size()==inventory.entries.size() && serial.totalBytes==inventory.totalBytes,"parallel inventory count");
    for(std::size_t i=0;i<serial.entries.size();++i)
        check(serial.entries[i].logicalPath==inventory.entries[i].logicalPath && serial.entries[i].identity==inventory.entries[i].identity,"deterministic parallel inventory");
    std::stop_source cancelled;cancelled.request_stop();
    rejects([&]{ObjectPackager::inventory(maps,cancelled.get_token());});
    {
    ObjectStore store(root/"objects","packager-container",true);ObjectPackager packager(store);
    auto session=store.beginSession({"local:society","packager","Packager"},"fixture","migration");
    std::stop_source stop;std::size_t progress=0;
    auto partial=packager.package(inventory,session.key,stop.get_token(),[&](const auto &,const auto &,const char *){++progress;stop.request_stop();});
    check(partial.cancelled && partial.imported==1 && store.count()==1 && progress==1,"partial checkpoint");
    auto resumed=packager.package(inventory,session.key);
    check(resumed.imported==1 && resumed.skipped==1 && resumed.issues.empty() && store.count()==2,"resume preserves completed objects");
    const auto before=store.lookupPath("Files/sub/a.txt").value();
    auto again=packager.package(inventory,session.key);check(again.skipped==2 && again.imported==0 && store.history(before.key).size()==1,"idempotent resume");
    write(root/"tree/sub/a.txt","latest contents");
    auto stale=packager.package(inventory,session.key);check(stale.issues.size()==1 && store.lookup(before.key)->version==1,"stale inventory does not publish");
    inventory=ObjectPackager::inventory(maps);
    auto updated=packager.package(inventory,session.key);
    check(updated.revised==1 && updated.skipped==1 && updated.issues.empty(),"changed source gets new version");
    const auto after=store.lookup(before.key).value();check(after.version==2 && after.sourceStamp==ObjectSource::inspect(root/"tree/sub/a.txt").stamp,"current source binding");
    store.extract(before.key,1,root/"old");store.extract(before.key,2,root/"new");
    check(read(root/"old")=="after" && read(root/"new")=="latest contents","package history reconstructs file edits");
    check(fs::is_empty(root/"objects/staging"),"no staging leak");
    {
        ObjectStore bulk(root/"bulk","bulk-container",true);ObjectPackager bulkPackager(bulk);
        const auto bulkSession=bulk.beginSession({"local:society","packager","Packager"},"test","small-file batching");
        std::size_t callbacks=0;
        const auto bulkResult=bulkPackager.package(inventory,bulkSession.key,{},[&](const auto &,const auto &,const char *){
            ++callbacks;check(bulk.count()==2,"batch callbacks run only after complete commit");
        },{},ObjectStore::maximumBatchFiles);
        check(bulkResult.imported==2 && bulkResult.issues.empty() && callbacks==2,"small-file batch packaging");
        check(fs::is_empty(root/"bulk/staging"),"batch snapshots cleaned");
        rejects([&]{bulkPackager.package(inventory,bulkSession.key,{}, {}, {}, 0);});
        // Reuse the open store/session: this tests the batch boundary, not
        // another durable database initialization on the fixture volume.
        fs::create_directory(root/"metadata-tree");
        for(unsigned i=0;i<17;++i)write(root/"metadata-tree"/std::to_string(i),"small");
        const auto metadataInventory=ObjectPackager::inventory({{"Metadata",root/"metadata-tree",{}}});
        std::string document=R"({"schemaVersion":3,"authors":[],"participants":[],"links":[]})";
        document.resize(1024*1024,' ');
        std::size_t metadataCallbacks=0;
        const auto metadataResult=bulkPackager.package(metadataInventory,bulkSession.key,{},
            [&](const auto &entry,const auto &record,const char *){
                ++metadataCallbacks;
                check(bulk.count()==(metadataCallbacks<=16?18:19),"metadata budget splits atomic batches");
                check(record.sourceStamp==entry.identity.stamp && record.authorship.payload==document,
                      "rollover preserves source stamp and exact provenance");
            },[&](const auto &)->std::optional<ObjectMetadata>{return ObjectMetadata{"iiFileProvider.Authorship/3",document};},
            ObjectStore::maximumBatchFiles);
        check(metadataResult.imported==17 && metadataResult.issues.empty() && metadataCallbacks==17,
              "metadata rollover preserves full packaging scope");
        check(fs::is_empty(root/"bulk/staging"),"metadata rollover snapshots cleaned");
        fs::create_directory(root/"changing-tree");
        write(root/"changing-tree/0","original");write(root/"changing-tree/1","second");
        const auto changing=ObjectPackager::inventory({{"Changing",root/"changing-tree",{}}});
        const auto owner=std::this_thread::get_id();
        const auto rejectedBatch=bulkPackager.package(changing,bulkSession.key,{}, {},
            [&](const auto &entry)->std::optional<ObjectMetadata>{
                check(std::this_thread::get_id()==owner,"metadata callbacks remain on owning thread");
                if(entry.logicalPath=="Changing/1")write(root/"changing-tree/0","changed during preparation");
                return std::nullopt;
            },ObjectStore::maximumBatchFiles);
        check(rejectedBatch.imported==0 && rejectedBatch.issues.size()==2 && bulk.count()==19,
              "snapshot acquisition failure rejects every selected batch member");
        check(fs::is_empty(root/"bulk/staging"),"failed parallel snapshot batch cleaned");
        const auto refreshed=ObjectPackager::inventory({{"Changing",root/"changing-tree",{}}});
        const auto retried=bulkPackager.package(refreshed,bulkSession.key,{}, {}, {},ObjectStore::maximumBatchFiles);
        check(retried.imported==2 && retried.issues.empty() && bulk.count()==21,"fresh inventory retries complete rejected batch");
        bulk.endSession(bulkSession.key);
    }
    test_support::create_symlink(root/"tree/b.txt",root/"tree/unsafe");
    auto bad=ObjectPackager::inventory(maps);check(bad.issues.size()==1,"symlink reported");
    auto declined=packager.package(bad,session.key);check(declined.imported==0 && !declined.issues.empty(),"incomplete inventory cannot claim migration");
    rejects([&]{ObjectPackager::inventory({maps[0],maps[0]});});
    store.endSession(session.key);
    }
    fs::remove_all(root);std::cout<<"Snapshot and resumable packager passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
