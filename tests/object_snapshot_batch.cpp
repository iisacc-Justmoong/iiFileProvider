#include "native_symlink.h"
#include "ObjectSnapshotBatch.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs=std::filesystem;
using namespace iiFileProvider;
void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
void write(const fs::path &p,const std::string &text){std::ofstream f(p,std::ios::binary);f<<text;check(bool(f),"fixture write");}
std::string read(const fs::path &p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
template<class F>void rejects(F f){bool failed=false;try{f();}catch(const std::exception &){failed=true;}check(failed,"snapshot batch must reject");}
int main(){try{
    const auto root=fs::path(OBJECT_TEST_DIRECTORY)/("snapshot-batch-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);fs::create_directory(root/"stage");
    std::vector<detail::ObjectSnapshotRequest> requests;
    for(unsigned i=0;i<16;++i){
        const auto path=root/std::to_string(i);
        std::string payload(i==15?2*1024*1024+17:i*107,'a'+i);
        if(i==15){payload[1024*1024-1]='X';payload[1024*1024]='Y';payload.back()='Z';}
        write(path,payload);requests.push_back({path,ObjectSource::inspect(path)});
    }
    for(std::size_t concurrency:{1,4}){
        {
            detail::ObjectSnapshotBatch batch(requests,root/"stage",{},concurrency);
            for(std::size_t i=0;i<requests.size();++i)check(read(batch.path(i))==read(requests[i].source),"parallel snapshot bytes and order");
            write(requests[3].source,"modified original");
            check(read(batch.path(3))==std::string(3*107,'d'),"snapshot isolated from later writes");
            rejects([&]{batch.path(requests.size());});
        }
        check(fs::is_empty(root/"stage"),"all batch-owned staging removed before return");
        write(requests[3].source,std::string(3*107,'d'));requests[3].identity=ObjectSource::inspect(requests[3].source);
    }
    auto invalid=requests;invalid[5].identity.stamp="stale";
    rejects([&]{ObjectSource source(invalid[5].source,root/"stage",{},invalid[5].identity);});
    check(fs::is_empty(root/"stage"),"expected identity checked before staging allocation");
    rejects([&]{detail::ObjectSnapshotBatch batch(invalid,root/"stage",{},4);});
    check(fs::is_empty(root/"stage"),"stale member failure cleans every captured snapshot");
    std::stop_source cancellation;cancellation.request_stop();
    rejects([&]{detail::ObjectSnapshotBatch batch(requests,root/"stage",cancellation.get_token(),4);});
    rejects([&]{detail::ObjectSnapshotBatch batch({},root/"stage");});
    invalid.assign(257,requests[0]);rejects([&]{detail::ObjectSnapshotBatch batch(invalid,root/"stage");});
    invalid={requests[0]};invalid[0].identity.size=17*1024*1024;
    rejects([&]{detail::ObjectSnapshotBatch batch(invalid,root/"stage");});
    // One worker fails after other workers may already own stable snapshots.
    invalid=requests;invalid[7].source=root/"absent";
    rejects([&]{detail::ObjectSnapshotBatch batch(invalid,root/"stage",{},1);});
    rejects([&]{detail::ObjectSnapshotBatch batch(invalid,root/"stage",{},4);});
    check(fs::is_empty(root/"stage"),"failed acquisition leaves no owned staging");
    test_support::create_symlink(requests[0].source,root/"link");invalid=requests;invalid[6].source=root/"link";
    rejects([&]{detail::ObjectSnapshotBatch batch(invalid,root/"stage",{},4);});
    check(fs::is_empty(root/"stage"),"redirected acquisition cleans successful workers");
    write(root/"stage/unrelated","preserve");
    {detail::ObjectSnapshotBatch batch(requests,root/"stage",{},4);}
    check(read(root/"stage/unrelated")=="preserve","cleanup only removes batch-owned snapshots");
    fs::remove_all(root);std::cout<<"Bounded snapshot acquisition, immutable content, error cleanup and ownership passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
