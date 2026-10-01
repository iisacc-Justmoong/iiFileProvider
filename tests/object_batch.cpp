#include "ObjectStore.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace iiFileProvider;
namespace fs=std::filesystem;
constexpr unsigned batchCount=256;
static_assert(ObjectStore::maximumBatchFiles==batchCount);
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F function){bool rejected=false;try{function();}catch(const std::exception &){rejected=true;}check(rejected,"batch must reject");}
int main(){try{
    const auto root=fs::current_path()/("object-batch-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    {std::ofstream out(root/"source");out<<"same content, distinct objects";}
    std::vector<char> boundary(ObjectStore::chunkBytes+17);
    for(std::size_t i=0;i<boundary.size();++i)boundary[i]=static_cast<char>((i*31)%251);
    {std::ofstream out(root/"boundary",std::ios::binary);out.write(boundary.data(),boundary.size());}
    {std::ofstream out(root/"empty",std::ios::binary);}
    {
        ObjectStore store(root/"package","batch-fixture",true);
        auto session=store.beginSession({"local:test","ingester","Ingester"},"test","atomic batch");
        std::vector<ObjectImport> inputs;
        for(unsigned i=0;i<batchCount;++i){
            ObjectWriteOptions options;options.sourceStamp="stamp-"+std::to_string(i);
            inputs.push_back({root/(i==0?"boundary":i==1?"empty":"source"),"Files/"+std::to_string(i),options});
        }
        const auto records=store.importFiles(inputs,session.key);
        check(records.size()==batchCount && store.count()==batchCount && store.index().size()==batchCount,"batch publishes every object");
        for(std::size_t i=0;i<records.size();++i){
            check(records[i].path==inputs[i].logicalPath && records[i].sourceStamp==inputs[i].options.sourceStamp
                && records[i].version==1 && store.validate(records[i].key,1),"batch metadata and content");
            if(i)check(records[i].key!=records[i-1].key && records[i].indexKey>records[i-1].indexKey,"independent identities");
        }
        store.extract(records[0].key,1,root/"extracted-boundary");
        {std::ifstream input(root/"extracted-boundary",std::ios::binary);
         const std::vector<char> actual{std::istreambuf_iterator<char>(input),{}};
         check(actual==boundary,"prepared batch preserves binary chunk boundary and tail");}
        check(records[1].size==0 && records[1].sha256=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
              "prepared empty object uses canonical empty digest");
        check(records[0].sha256!=records[2].sha256,"different prepared sources retain different payloads");
        for(std::size_t i=3;i<records.size();++i)check(records[i].sha256==records[2].sha256,"prepared source ordering preserved");
        rejects([&]{store.importFiles({{root/"source","Files/new",{}},{root/"missing","Files/missing",{}}},session.key);});
        check(!store.lookupPath("Files/new") && store.count()==batchCount,"one missing source rolls back all objects");
        rejects([&]{store.importFiles({{root/"source","Files/new",{}},{root/"source","Files/0",{}}},session.key);});
        check(!store.lookupPath("Files/new") && store.count()==batchCount,"path conflict rolls back earlier batch entries");
        ObjectWriteOptions invalid;invalid.authorship=ObjectMetadata{"iiFileProvider.Authorship/3","not-json"};
        rejects([&]{store.importFiles({{root/"source","Files/new",{}},{root/"source","Files/invalid",invalid}},session.key);});
        check(!store.lookupPath("Files/new") && store.count()==batchCount,"invalid metadata rolls back batch");
        std::stop_source stopped;stopped.request_stop();ObjectWriteOptions cancelled;cancelled.cancellation=stopped.get_token();
        rejects([&]{store.importFiles({{root/"source","Files/new",{}},{root/"source","Files/cancelled",cancelled}},session.key);});
        check(!store.lookupPath("Files/new") && store.count()==batchCount,"cancellation rolls back batch");
        rejects([&]{store.importFiles({},session.key);});
        std::vector<ObjectImport> oversized;
        for(unsigned i=0;i<=batchCount;++i)oversized.push_back({root/"source","Files/oversized-"+std::to_string(i),{}});
        rejects([&]{store.importFiles(oversized,session.key);});
        check(!store.lookupPath("Files/oversized-0"),"count limit rejects before any publication");
        std::string document=R"({"schemaVersion":3,"authors":[],"participants":[],"links":[]})";
        document.resize(1024*1024,' ');
        ObjectWriteOptions provenance;provenance.authorship=ObjectMetadata{"iiFileProvider.Authorship/3",document};
        std::vector<ObjectImport> metadataHeavy;
        for(unsigned i=0;i<17;++i)metadataHeavy.push_back({root/"source","Files/metadata-"+std::to_string(i),provenance});
        rejects([&]{store.importFiles(metadataHeavy,session.key);});
        check(!store.lookupPath("Files/metadata-0") && store.count()==batchCount,"metadata budget rejects the entire batch");
        {std::ofstream large(root/"large");large.seekp(16*1024*1024);large.put('x');}
        rejects([&]{store.importFiles({{root/"large","Files/too-large",{}}},session.key);});
        store.endSession(session.key);
        rejects([&]{store.importFiles({{root/"source","Files/closed",{}}},session.key);});
    }
    fs::remove_all(root);std::cout<<"Small-file atomic batch passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
