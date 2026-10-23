#include "FileCatalog.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace iiFileProvider;
namespace fs=std::filesystem;
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
int main()try{
    const auto root=fs::path(CATALOG_TEST_DIRECTORY)/("catalog-reconcile-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));fs::create_directory(root);
    {FileCatalog catalog(root);FileMetadata metadata;metadata.title="Launch";metadata.keywords={"approved"};
    const std::string bytes="payload";auto first=catalog.createFile("first.txt",{reinterpret_cast<const std::byte*>(bytes.data()),bytes.size()},metadata);
    auto other=catalog.createFile("other.txt",{reinterpret_cast<const std::byte*>(bytes.data()),bytes.size()});
    auto page=catalog.search({}, {},1);check(page.next.has_value(),"cursor fixture");
    const auto again=catalog.registerFile("first.txt");check(again.file.revision==first.file.revision,"unchanged registration retains revision");
    check(catalog.search({},page.next,1).files.size()==1,"no-op registration retains search cursor");
    fs::rename(root/"first.txt",root/"renamed.txt");fs::rename(root/"first.txt.iisacc.json",root/"renamed.txt.iisacc.json");
    auto moved=catalog.registerFile("renamed.txt");check(moved.file.key==first.file.key && moved.file.metadata==metadata && moved.metadataWritten,"native rename preserves catalog identity");
    fs::copy_file(root/"renamed.txt",root/"copy.txt");fs::copy_file(root/"renamed.txt.iisacc.json",root/"copy.txt.iisacc.json");
    auto copied=catalog.registerFile("copy.txt");check(copied.file.key!=first.file.key && copied.file.metadata==metadata && copied.metadataWritten,"copied companion forks identity while preserving annotation");
    check(catalog.search(FileFilter{.text="approved"}).files.size()==2,"original and duplicate use FTS index");
    fs::create_directory(root/"folder");
    auto nested=catalog.createFile("folder/nested.txt",{reinterpret_cast<const std::byte*>(bytes.data()),bytes.size()},metadata);
    fs::rename(root/"folder",root/"archive");
    auto renamedTree=catalog.registerFile("archive/nested.txt");
    check(renamedTree.file.key==nested.file.key && renamedTree.metadataWritten,"renamed ancestor is not recreated or treated as a redirected file");
    check(!fs::exists(root/"folder"),"reconciliation never recreates missing old ancestors");
    }fs::remove_all(root);std::cout<<"No-op, rename and copied companion reconciliation verified\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
