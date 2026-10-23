#include "ObjectStore.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace iiFileProvider;
namespace fs=std::filesystem;
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F f){bool no=false;try{f();}catch(const std::exception&){no=true;}require(no,"expected rejection");}
int main()try{
    const auto root=fs::path(OBJECT_TEST_DIRECTORY)/("document-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);{std::ofstream out(root/"input.raw");out<<"original bytes";}
    std::string key;
    {
        ObjectStore store(root/"objects","documents",true);
        const auto session=store.beginSession({"local:test","editor","Editor"},"device","edit",{"test","1","project","document"});
        auto document=store.importDocument(root/"input.raw","files/input.raw",{"input.raw"},session.key);key=document.file.key;
        require(store.count()==2 && store.journal().size()==2,"original and working object publish together");
        require(document.file.key!=document.original.record.key && document.file.sha256==document.original.record.sha256,"distinct identity and shared content");
        require(store.original(key).record.key==document.original.record.key,"original lookup");
        rejects([&]{store.importDocument(root/"input.raw","files/input.raw",{"input.raw"},session.key);});
        require(store.count()==2 && store.journal().size()==2,"collision rolls back both objects");
        {std::ofstream out(root/"input.raw");out<<"edited bytes";}
        auto edited=store.reviseFile(key,root/"input.raw",1,session.key);
        require(edited.version==2 && edited.sha256!=document.original.record.sha256,"working object is editable");
        store.extract(document.original.record.key,1,root/"recovered.raw");
        require(fs::file_size(root/"recovered.raw")==14,"original bytes remain recoverable");
        rejects([&]{store.reviseFile(document.original.record.key,root/"input.raw",1,session.key);});
        FileMetadata metadata;metadata.keywords={"approved"};
        auto annotation=store.reviseMetadata(document.original.record.key,metadata,1,session.key);
        require(annotation.version==2 && store.original(key).metadata.annotations==metadata,"metadata journal belongs to original envelope");
        auto moved=store.move(key,"published/result.raw",2,session.key,{{},"verified-native-relocation"});
        require(moved.sourceStamp=="verified-native-relocation" && moved.sha256==edited.sha256 && store.diff(key,3).empty(),"native relocation reuses stored chunks");
        store.erase(key,3,session.key);
        require(store.original(key).record.sha256==document.original.record.sha256,"original survives move and tombstone");
        require(store.validate(key,4) && store.validate(annotation.key,2),"both validation chains hold");
        store.endSession(session.key);
    }
    {ObjectStore store(root/"objects","documents",ObjectStore::Access::ReadOnly);require(store.original(key).metadata.annotations.keywords[0]=="approved","original survives reopen");}
    fs::remove_all(root);std::cout<<"Atomic RAW original and mutable document verified\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
