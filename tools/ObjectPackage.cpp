#include "ObjectPackageOptions.h"
#include <algorithm>
#include <chrono>
#include <csignal>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace iiFileProvider;
using iiFileProvider::cli::PackageMode;
namespace {
volatile std::sig_atomic_t interrupted=0;
void interrupt(int){interrupted=1;}
std::string json(const std::string &s){
    constexpr char hex[]="0123456789abcdef";std::string out="\"";
    for(const unsigned char c:s){
        if(c=='"' || c=='\\'){out+='\\';out+=char(c);}
        else if(c<32){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}
        else out+=char(c);
    }
    return out+'"';
}
void issue(const ObjectPackagingIssue &value){std::cout<<"{\"event\":\"error\",\"path\":"<<json(value.path)<<",\"message\":"<<json(value.message)<<"}\n"<<std::flush;}
}
int main(int argc,char **argv){try{
    const std::vector<std::string_view> arguments(argv+1,argv+argc);
    const auto options=cli::parseObjectPackageOptions(arguments);
    std::signal(SIGINT,interrupt);std::signal(SIGTERM,interrupt);
    std::stop_source cancellation;
    std::jthread observer([&](std::stop_token stop){while(!stop.stop_requested()){if(interrupted){cancellation.request_stop();return;}std::this_thread::sleep_for(std::chrono::milliseconds(50));}});
    if(options.mode==PackageMode::Index){
        ObjectStore store(options.package,options.container,ObjectStore::Access::ReadOnly);
        for(int run=0;run<3;++run){
            const auto started=std::chrono::steady_clock::now();std::int64_t cursor=0;std::size_t count=0;
            for(;;){auto page=store.index(cursor,1024);if(page.empty())break;count+=page.size();cursor=page.back().indexKey;}
            const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
            std::cout<<"{\"event\":\"index\",\"run\":"<<run+1<<",\"objects\":"<<count<<",\"milliseconds\":"<<elapsed<<"}\n"<<std::flush;
        }
        return 0;
    }
    if(options.mode==PackageMode::EndSession){
        ObjectStore store(options.package,options.container,false,options.walAutoCheckpointPages);
        store.endSession(options.sessionToEnd);
        std::cout<<"{\"event\":\"session-ended\",\"key\":"<<json(options.sessionToEnd)<<"}\n"<<std::flush;
        return 0;
    }
    const auto started=std::chrono::steady_clock::now();
    std::cout<<"{\"event\":\"inventory-start\"}\n"<<std::flush;
    auto inventory=ObjectPackager::inventory(options.mappings,cancellation.get_token());
    std::cout<<"{\"event\":\"inventory\",\"files\":"<<inventory.entries.size()<<",\"bytes\":"<<inventory.totalBytes<<",\"issues\":"<<inventory.issues.size()
        <<",\"seconds\":"<<std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()<<"}\n"<<std::flush;
    for(const auto &error:inventory.issues)issue(error);
    if(options.mode==PackageMode::Inventory || !inventory.issues.empty())return inventory.issues.empty()?0:1;
    ObjectStore store(options.package,options.container,options.mode==PackageMode::Audit?ObjectStore::Access::ReadOnly:ObjectStore::Access::Create,options.walAutoCheckpointPages);
    ObjectPackager packager(store);
    if(options.mode==PackageMode::Audit){
        const auto report=packager.audit(inventory,cancellation.get_token(),[](const auto &entry,const auto &key){
            std::cout<<"{\"event\":\"verified\",\"path\":"<<json(entry.logicalPath)<<",\"key\":"<<json(key)<<"}\n"<<std::flush;
        });
        for(const auto &error:report.issues)issue(error);
        std::cout<<"{\"event\":\"audit\",\"files\":"<<inventory.entries.size()<<",\"verified\":"<<report.verified
            <<",\"errors\":"<<report.issues.size()<<",\"cancelled\":"<<(report.cancelled?"true":"false")<<"}\n"<<std::flush;
        return report.cancelled?130:report.issues.empty()?0:1;
    }
    std::cout<<"{\"event\":\"settings\",\"walAutoCheckpointPages\":"<<options.walAutoCheckpointPages<<"}\n"<<std::flush;
    // Publish the greatest number of independent objects early. A model whose
    // name sorts first must not delay every thumbnail/sidecar behind its bytes.
    // Size only schedules ingestion; identities, source checks and scope do not change.
    std::sort(inventory.entries.begin(),inventory.entries.end(),[](const auto &a,const auto &b){
        return a.identity.size==b.identity.size?a.logicalPath<b.logicalPath:a.identity.size<b.identity.size;
    });
    const auto session=store.beginSession({"local:society","iiFileProvider.packager","Society packaging service"},"local","Resumable Society object packaging; original authorship is unknown unless supplied");
    std::cout<<"{\"event\":\"session\",\"key\":"<<json(session.key)<<"}\n"<<std::flush;
    ObjectPackagingReport report;
    try{
        report=packager.package(inventory,session.key,cancellation.get_token(),[&](const auto &entry,const auto &record,const char *operation){
            std::cout<<"{\"event\":"<<json(operation)<<",\"path\":"<<json(entry.logicalPath)
                <<",\"key\":"<<json(record.key)<<",\"indexKey\":"<<record.indexKey
                <<",\"version\":"<<record.version<<",\"bytes\":"<<record.size
                <<",\"sha256\":"<<json(record.sha256)<<",\"validationKey\":"<<json(record.validationKey)
                <<",\"sessionKey\":"<<json(record.sessionKey)
                <<",\"recordingActor\":{\"origin\":"<<json(record.author.origin)
                <<",\"subject\":"<<json(record.author.subject)<<",\"name\":"<<json(record.author.displayName)<<"}"
                <<",\"authorshipKnown\":"<<(record.authorship.payload.empty()?"false":"true")<<"}\n"<<std::flush;
        },{},ObjectStore::maximumBatchFiles);
        store.endSession(session.key);
    }catch(...){store.endSession(session.key);throw;}
    for(const auto &error:report.issues)issue(error);
    const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    std::cout<<"{\"event\":\"summary\",\"imported\":"<<report.imported<<",\"revised\":"<<report.revised<<",\"skipped\":"<<report.skipped<<",\"errors\":"<<report.issues.size()<<",\"cancelled\":"<<(report.cancelled?"true":"false")<<",\"seconds\":"<<elapsed<<"}\n"<<std::flush;
    return report.cancelled?130:report.issues.empty()?0:1;
}catch(const std::exception &e){issue({"",e.what()});return interrupted?130:1;}}
