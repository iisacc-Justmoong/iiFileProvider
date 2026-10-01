#include "ObjectBatchRead.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace iiFileProvider;
namespace fs=std::filesystem;
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F function){bool failed=false;try{function();}catch(const std::exception &){failed=true;}check(failed,"invalid batch read must reject");}
int main(){try{
    const auto root=fs::current_path()/("batch-read-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    const std::vector<std::size_t> sizes{0,1,ObjectStore::chunkBytes-1,ObjectStore::chunkBytes,ObjectStore::chunkBytes+1,2*ObjectStore::chunkBytes+123};
    std::vector<std::vector<std::uint8_t>> expected;
    std::vector<ObjectImport> files;
    for(std::size_t i=0;i<sizes.size();++i){
        auto &bytes=expected.emplace_back(sizes[i]);
        for(std::size_t n=0;n<bytes.size();++n)bytes[n]=static_cast<std::uint8_t>((n*31+i*17)%251);
        const auto path=root/std::to_string(i);
        std::ofstream output(path,std::ios::binary);
        if(!bytes.empty())output.write(reinterpret_cast<const char *>(bytes.data()),bytes.size());
        check(bool(output),"fixture write");
        files.push_back({path,"Files/"+std::to_string(i),{}});
    }
    const auto serial=detail::readObjectBatch(files,1);
    const auto parallel=detail::readObjectBatch(files,4);
    check(serial.size()==files.size() && parallel.size()==files.size(),"complete bounded batch read");
    for(std::size_t i=0;i<files.size();++i){
        check(serial[i].bytes==expected[i] && parallel[i].bytes==expected[i],"binary payload order and chunk tails preserved");
        check(parallel[i].source==files[i].source && parallel[i].identity==ObjectSource::inspect(files[i].source),"source identity bound to captured bytes");
    }
    // When the platform enforces POSIX read permissions, exercise an actual
    // worker-side failure after metadata preflight with parallel reading enabled.
    const auto permissions=fs::status(files[2].source).permissions();
    fs::permissions(files[2].source,fs::perms::owner_write,fs::perm_options::replace);
    std::ifstream denied(files[2].source,std::ios::binary);
    const bool enforced=!denied.is_open();denied.close();
    bool workerRejected=false;
    if(enforced){
        check(ObjectSource::inspect(files[2].source).size==sizes[2],"unreadable payload still passes metadata inspection");
        try{detail::readObjectBatch(files,4);}catch(const std::exception &){workerRejected=true;}
    }
    fs::permissions(files[2].source,permissions,fs::perm_options::replace);
    if(enforced)check(workerRejected,"worker read failure rejects after joining all workers");
    std::cout<<(enforced?"Worker read-denial contract exercised\n":"Worker read-denial contract skipped: permissions not enforced\n");
    rejects([&]{detail::readObjectBatch({});});
    auto missing=files;missing[2].source=root/"absent";
    rejects([&]{detail::readObjectBatch(missing,4);});
    std::stop_source stop;stop.request_stop();
    auto cancelled=files;cancelled[2].options.cancellation=stop.get_token();
    rejects([&]{detail::readObjectBatch(cancelled,4);});
    {std::ofstream large(root/"large",std::ios::binary);large.seekp(9*1024*1024-1);large.put('x');}
    rejects([&]{detail::readObjectBatch({{root/"large","Files/a",{}},{root/"large","Files/b",{}}},4);});
    std::vector<ObjectImport> tooMany(ObjectStore::maximumBatchFiles+1,files[0]);
    rejects([&]{detail::readObjectBatch(tooMany,4);});
    fs::create_symlink(files[1].source,root/"redirect");
    rejects([&]{detail::readObjectBatch({{root/"redirect","Files/link",{}}},4);});
    fs::remove_all(root);
    std::cout<<"Bounded serial/parallel object reads passed\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
