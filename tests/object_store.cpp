#include "native_symlink.h"
#include "ObjectStore.h"
#include "ObjectHash.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <future>
#include <sqlite3.h>

using namespace iiFileProvider;
namespace fs=std::filesystem;
void require(bool value,const char *message) {if(!value) throw std::runtime_error(message);}
template<class F> void rejected(F f) {bool failed=false;try{f();}catch(const std::exception &){failed=true;} require(failed,"operation should fail");}
void write(const fs::path &p,const std::string &s) {std::ofstream f(p,std::ios::binary);f<<s;require(bool(f),"fixture write");}
std::string read(const fs::path &p) {std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int main() {try {
    detail::ObjectHash empty;require(empty.finish()=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","empty SHA256");
    detail::ObjectHash abc;abc.update("a");abc.update("bc");require(abc.finish()=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","abc SHA256");
    detail::ObjectHash million;for(int i=0;i<1000;++i)million.update(std::string(1000,'a'));
    require(million.finish()=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0","streaming SHA256");
    const auto root=fs::current_path()/ ("object-store-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    {
        const auto unicodePackage = root / fs::path(u8"한글-package.sobj");
        ObjectStore created(unicodePackage, "unicode-container", true);
        require(fs::exists(unicodePackage), "Unicode package created");
        require(fs::file_size(unicodePackage / "objects.sqlite3")>0, "Unicode database initialized");
        const auto unknown = root / "unknown-package";
        fs::create_directory(unknown);
        write(unknown / "objects.sqlite3", "existing bytes");
        rejected([&]{ ObjectStore duplicate(unknown, "unicode-container", true); });
        require(read(unknown / "objects.sqlite3")=="existing bytes", "creation preserves unknown database");
    }
    const auto source=root/"original.bin";
    std::string original(ObjectStore::chunkBytes+17,'x');
    original[ObjectStore::chunkBytes-1]='A';original[ObjectStore::chunkBytes]='B';original.back()='C';
    write(source,original);
    ObjectAuthor author{"https://iisacc.com","test-subject","Test Author"};
    std::string key,sessionId;
    {
        ObjectStore store(root/"package","test-container",true);
        const auto session=store.beginSession(author,"test-device","object-store contract");sessionId=session.key;
        auto first=store.importFile(source,"Files/文書.bin",session.key);key=first.key;
        require(first.version==1 && first.indexKey>0 && first.author==author,"object metadata");
        require(store.validate(key,1),"initial validation");require(store.count()==1,"object count");
        require(store.lookupPath("Files/文書.bin")->key==key,"path mapping");
        rejected([&]{store.importFile(source,"Files/文書.bin",session.key);});
        rejected([&]{store.importFile(source,"../escape",session.key);});
        test_support::create_symlink(source,root/"redirect");rejected([&]{store.importFile(root/"redirect","Files/redirect",session.key);});
        require(store.count()==1,"failed import rollback");
        auto changed=original;changed[5]='y';write(source,changed);
        rejected([&]{store.reviseFile(key,source,0,session.key);});
        auto second=store.reviseFile(key,source,1,session.key);
        require(second.version==2 && second.indexKey==first.indexKey && second.sha256!=first.sha256,"revision identity");
        const auto delta=store.diff(key,2);require(delta.size()==1 && delta[0].offset==0 && delta[0].beforeHash!=delta[0].afterHash,"chunk diff");
        store.extract(key,1,root/"restored-v1");store.extract(key,2,root/"restored-v2");
        require(read(root/"restored-v1")==original && read(root/"restored-v2")==changed,"reversible versions");
        rejected([&]{store.extract(key,1,source);});require(read(source)==changed,"never overwrite original");
        auto moved=store.move(key,"Published/renamed.bin",2,session.key);
        require(moved.key==key && moved.version==3 && store.diff(key,3).empty(),"move independent identity");
        require(!store.lookupPath("Files/文書.bin"),"old path removed");
        require(store.history(key).size()==3 && store.validate(key,3),"journal validation");
        auto removed=store.erase(key,3,session.key);
        require(removed.deleted && store.count()==0 && store.count(true)==1,"tombstone");
        require(store.scan().empty() && store.scan(0,10,true).size()==1,"indexed live/tombstone scan");
        require(store.index().empty() && store.index(0,10,true).size()==1 && store.index(0,10,true)[0].deleted,"compact tombstone index");
        require(store.validate(key,4),"tombstone validation");
        rejected([&]{store.extract(key,4,root/"deleted");});
        store.endSession(session.key);require(store.session(session.key).endedAtNs>0,"session closure");
        rejected([&]{store.importFile(source,"Files/new.bin",session.key);});
    }
    {ObjectStore reopened(root/"package","test-container");require(reopened.history(key).size()==4 && reopened.validate(key,2),"persisted package");}
    rejected([&]{ObjectStore wrong(root/"package","another-container");});
    {
        ObjectStore store(root/"package","test-container");
        const auto work=store.beginSession(author,"device","boundary checks");
        rejected([&]{store.importFile(root/"missing","Files/missing",work.key);});
        require(store.count(true)==1,"failed payload rolls back index allocation");
        write(root/"empty","");
        const auto emptyRecord=store.importFile(root/"empty","Files/empty",work.key);
        require(store.validate(emptyRecord.key,1) && store.diff(emptyRecord.key,1).empty(),"empty object");
        store.extract(emptyRecord.key,1,root/"empty-out");require(read(root/"empty-out").empty(),"empty extraction");
        auto tiny=store.importFile(root/"empty","Files/another-empty",work.key);
        require(tiny.key!=emptyRecord.key,"equal content has independent identity");
        rejected([&]{store.move(tiny.key,"Files/empty",1,work.key);});
        require(store.lookup(tiny.key)->version==1 && store.history(tiny.key).size()==1,"move conflict is atomic");
        auto page=store.scan(0,1);require(page.size()==1 && page[0].key==emptyRecord.key,"first indexed page");
        auto next=store.scan(page[0].indexKey,1);require(next.size()==1 && next[0].key==tiny.key,"next indexed page");
        require(store.scan(next[0].indexKey,1).empty(),"keyset completion");
        const auto compact=store.index(0,1);
        require(compact.size()==1 && compact[0].key==emptyRecord.key && compact[0].sha256==emptyRecord.sha256
            && compact[0].validationKey==emptyRecord.validationKey && compact[0].sessionKey==emptyRecord.sessionKey
            && compact[0].authorSubject==emptyRecord.author.subject,"compact index metadata");
        const auto compactNext=store.index(compact[0].indexKey,1);
        require(compactNext.size()==1 && compactNext[0].key==tiny.key && store.index(compactNext[0].indexKey,1).empty(),"compact index keyset completion");
        ObjectStore competitor(root/"package","test-container");
        const auto concurrent=competitor.beginSession(author,"device-two","concurrent update");
        auto attempt=[&](ObjectStore &db,const std::string &session) {try{db.reviseFile(tiny.key,root/"empty",1,session);return true;}catch(const std::exception &){return false;}};
        auto one=std::async(std::launch::async,[&]{return attempt(store,work.key);});
        auto two=std::async(std::launch::async,[&]{return attempt(competitor,concurrent.key);});
        require(int(one.get())+int(two.get())==1,"one CAS writer wins across connections");
        require(store.history(tiny.key).size()==2 && store.validate(tiny.key,2),"concurrent journal remains intact");
    }
    // Validate indexes without looking at originals and reject metadata/payload corruption.
    fs::remove(source);
    {
        ObjectStore store(root/"package","test-container");
        require(store.count()==2 && store.validate(key,1),"package survives original removal");
        store.extract(key,1,root/"after-source-removal");require(read(root/"after-source-removal")==original,"self-contained payload");
    }
    sqlite3 *raw=nullptr;require(sqlite3_open((root/"package/objects.sqlite3").string().c_str(),&raw)==SQLITE_OK,"tamper fixture connection");
    require(sqlite3_exec(raw,"UPDATE chunks SET data=zeroblob(length(data))",nullptr,nullptr,nullptr)==SQLITE_OK,"tamper fixture write");
    sqlite3_close(raw);
    {ObjectStore store(root/"package","test-container");require(!store.validate(key,1),"corrupt payload rejected");rejected([&]{store.extract(key,1,root/"corrupt-out");});require(!fs::exists(root/"corrupt-out"),"no corrupt extraction published");}
    require(sqlite3_open((root/"package/objects.sqlite3").string().c_str(),&raw)==SQLITE_OK,"metadata tamper connection");
    require(sqlite3_exec(raw,"UPDATE revisions SET author_name='tampered' WHERE version=1",nullptr,nullptr,nullptr)==SQLITE_OK,"metadata tamper write");sqlite3_close(raw);
    {ObjectStore store(root/"package","test-container");require(!store.validate(key,4,false),"parent journal tamper rejected");}
    fs::create_directory(root/"legacy");
    require(sqlite3_open((root/"legacy/objects.sqlite3").string().c_str(),&raw)==SQLITE_OK,"legacy fixture connection");
    const auto fixture=read(fs::path(OBJECT_FIXTURES_DIRECTORY)/"object-store-v1.sql");
    require(sqlite3_exec(raw,fixture.c_str(),nullptr,nullptr,nullptr)==SQLITE_OK,"legacy fixture import");sqlite3_close(raw);
    rejected([&]{ObjectStore wrong(root/"legacy","wrong-container");});
    {
        ObjectStore legacy(root/"legacy","legacy-container");const auto old=legacy.lookupPath("Files/legacy.txt").value();
        require(old.schemaVersion==1 && old.validationKey=="society-object-v1:c5e89d5a30a4230663fcd04b4228fe4fb37e5cfbfff53c1e4653b5febe746089","legacy validation key preserved");
        require(legacy.validate(old.key,1),"old metadata and payload validate after schema upgrade");
        require(legacy.index().size()==1 && legacy.index()[0].validationKey==old.validationKey,"schema-1 compact index backfill");
        const auto work=legacy.beginSession(author,"new-device","schema upgrade");
        auto updated=legacy.reviseFile(old.key,root/"empty",1,work.key);
        require(updated.schemaVersion==2 && updated.key==old.key && legacy.validate(old.key,2),"mixed-version journal chain");
        legacy.extract(old.key,1,root/"legacy-restored");require(read(root/"legacy-restored")=="legacy","old version remains restorable");
    }
    // Test-owned, unique directory only. Never touches a Society drive.
    fs::remove_all(root);
    std::cout<<"Object store contract passed\n";return 0;
} catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}}
