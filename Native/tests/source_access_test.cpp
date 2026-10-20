#include "iiFileProviderNative.h"
#include "iiFileProviderPresentation.h"
#include <chrono>
#include <atomic>
#include <filesystem>
#include <future>
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
using namespace std::chrono_literals;
namespace fs=std::filesystem;
using namespace iiFileProvider::native;
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    const auto root=fs::path(argv[1])/("source-access-"+std::to_string(std::random_device{}()));
    fs::create_directories(root);
    const auto path=root/"source.bin", other=root/"other.bin";
    File::write(path,"unchanged source");File::write(other,"independent source");
    if(!Presentation::waitForIdle(5s))return 3;
    bool blocked=false,independent=false,reentrant=false;
    std::future<std::string> read;
    {
        SourceAccess access(path);
        std::lock_guard lock(access);
        std::promise<void> entered;
        auto started=entered.get_future();
        read=std::async(std::launch::async,[&]{entered.set_value();return File::read(path);});
        started.wait();
        blocked=read.wait_for(100ms)==std::future_status::timeout;
        auto unrelated=std::async(std::launch::async,[&]{return File::read(other);});
        independent=unrelated.wait_for(2s)==std::future_status::ready && unrelated.get()=="independent source";
        reentrant=File::read(path)=="unchanged source";
    }
    const bool finished=read.wait_for(2s)==std::future_status::ready && read.get()=="unchanged source";
    // An idle open stream may coexist with a write, but its descriptor must
    // not close while that transaction owns the same source identity.
    auto retained=std::make_unique<ReadFile>(path);
    bool closeBlocked=false;
    std::future<void> closing;
    {
        SourceAccess access(path);std::lock_guard lock(access);
        std::promise<void> entered;auto started=entered.get_future();
        closing=std::async(std::launch::async,[reader=std::move(retained),&entered]() mutable {entered.set_value();reader.reset();});
        started.wait();closeBlocked=closing.wait_for(100ms)==std::future_status::timeout;
    }
    closing.get();
    bool aliases=true;
    std::error_code error;const auto alias=root/"hardlink.bin";
    fs::create_hard_link(path,alias,error);
    if(!error) {
        std::future<std::string> linked;
        {
            SourceAccess access(path);
            std::lock_guard lock(access);
            std::promise<void> entered;auto started=entered.get_future();
            linked=std::async(std::launch::async,[&]{entered.set_value();return File::read(alias);});
            started.wait();aliases=linked.wait_for(100ms)==std::future_status::timeout;
        }
        aliases=aliases && linked.get()=="unchanged source";
    }
    const auto privateRoot=root/".renderer";
    fs::create_directory(privateRoot);
    const auto renderedSource=privateRoot/"source.raccess";
    File::write(renderedSource,"before rendering");
    std::promise<void> rendererEntered,releaseRenderer;
    auto rendering=rendererEntered.get_future(),release=releaseRenderer.get_future();
    Presentation::setRenderer(".raccess",[&](const fs::path &,auto &) -> std::optional<FilePreview> {
        rendererEntered.set_value();release.wait();return {};
    });
    auto prepared=std::async(std::launch::async,[&]{
        try {(void)Presentation::prepare(renderedSource);return false;}
        catch(const FileError &){return true;}
    });
    const bool enteredRenderer=rendering.wait_for(2s)==std::future_status::ready;
    auto writer=std::async(std::launch::async,[&]{
        SourceAccess access(renderedSource);std::lock_guard lock(access);
        std::ofstream output(renderedSource,std::ios::binary|std::ios::trunc);
        output<<"changed while rendering";output.close();return bool(output);
    });
    const bool rendererNonBlocking=writer.wait_for(500ms)==std::future_status::ready;
    releaseRenderer.set_value();
    const bool staleRejected=prepared.get(),written=writer.get();
    Presentation::setRenderer(".raccess",{});
    const auto concurrentSource=privateRoot/"concurrent.raccess";
    File::write(concurrentSource,"same source for both previews");
    std::atomic_int rendererCount{0};
    std::promise<void> bothEntered,releaseBoth;
    auto both=bothEntered.get_future();
    auto released=releaseBoth.get_future().share();
    Presentation::setRenderer(".raccess",[&](const fs::path &,auto &) -> std::optional<FilePreview> {
        if(rendererCount.fetch_add(1)==1)bothEntered.set_value();
        released.wait();return {};
    });
    const auto prepareConcurrent=[&]{
        try {(void)Presentation::prepare(concurrentSource);return true;}
        catch(const FileError &){return false;}
    };
    auto first=std::async(std::launch::async,prepareConcurrent);
    auto second=std::async(std::launch::async,prepareConcurrent);
    const bool concurrentEntered=both.wait_for(2s)==std::future_status::ready;
    releaseBoth.set_value();
    const bool firstPrepared=first.get(),secondPrepared=second.get();
    Presentation::setRenderer(".raccess",{});
    const bool idle=Presentation::waitForIdle(5s);
    Presentation::cancelPending(root);fs::remove_all(root);
    if(!blocked||!independent||!reentrant||!finished||!closeBlocked||!aliases||!idle
       ||!enteredRenderer||!rendererNonBlocking||!staleRejected||!written
       ||!concurrentEntered||!firstPrepared||!secondPrepared) {
        std::cerr<<"blocked="<<blocked<<" independent="<<independent<<" reentrant="<<reentrant
          <<" finished="<<finished<<" closeBlocked="<<closeBlocked<<" aliases="<<aliases<<" idle="<<idle
          <<" enteredRenderer="<<enteredRenderer<<" rendererNonBlocking="<<rendererNonBlocking
          <<" staleRejected="<<staleRejected<<" written="<<written
          <<" concurrentEntered="<<concurrentEntered<<" firstPrepared="<<firstPrepared
          <<" secondPrepared="<<secondPrepared<<'\n';return 1;
    }
    return 0;
}
