#include "TestWorkspace.h"
#include "FileCatalog.h"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace iiFileProvider;
namespace fs=std::filesystem;
void check(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
template<class F> void rejects(F&& action,const char* message) { try { action(); } catch(const std::exception&) {return;} throw std::runtime_error(message); }
void write(const fs::path& path,std::string_view text){std::ofstream f(path,std::ios::binary);f<<text;if(!f)throw std::runtime_error("fixture write failed");}
int main() try {
    const auto root=provider_test::root(CATALOG_TEST_DIRECTORY)/("catalog-package-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);fs::create_directories(root/"Character.iiproject/assets");
    write(root/"Character.iiproject/manifest.json","{\"revision\":1}");write(root/"Character.iiproject/assets/model.bin","model bytes");
    {
        FileCatalog catalog(root);FileMetadata metadata;metadata.title="Character project";metadata.description="Production goal";metadata.fields={{"type","iiproject"}};
        const auto first=catalog.registerPackage("Character.iiproject",metadata);
        check(catalog.itemKind(first.file.key)==CatalogEntryKind::Package && first.metadataWritten && catalog.count()==1,"package registered as one item");
        check(!FileCatalog::classificationDocument(root/"Character.iiproject").empty(),"package companion supports independent classification");
        check(first.file.size==0,"package size is unknown without walking payloads");
        auto row=catalog.search(FileFilter{.text="character"});check(row.files.size()==1&&catalog.itemKind(row.files[0].key)==CatalogEntryKind::Package,"search reports package kind");
        check(catalog.resolvePackage(first.file.key)==root/"Character.iiproject","registered package resolves");
#ifndef _WIN32
        // Presentation metadata and permission changes update ctime but do not
        // alter the manifest head. Such changes must not invalidate a package.
        fs::permissions(root/"Character.iiproject/manifest.json",fs::perms::owner_exec,fs::perm_options::add);
        check(catalog.resolvePackage(first.file.key,first.file.contentVersion)==root/"Character.iiproject","metadata-only manifest changes do not invalidate the package");
#endif
        auto again=catalog.registerPackage("Character.iiproject");check(again.file.key==first.file.key&&again.file.revision==first.file.revision,"unchanged registration is a no-op");
        rejects([&]{(void)catalog.read(first.file.key);},"package must not be byte-range read as an ordinary file");
        rejects([&]{(void)catalog.registerFile("Character.iiproject/assets/model.bin");},"registered package internals must remain opaque");
        rejects([&]{(void)catalog.createFile("Character.iiproject/assets/injected.bin",{});},"catalog cannot create independently managed file inside a package");
        check(!fs::exists(root/"Character.iiproject/assets/injected.bin"),"rejected nested create leaves package untouched");
        check(catalog.count()==1,"failed internal registration does not create a catalog item");
        write(root/"Character.iiproject/manifest.json","{\"revision\":2,\"changed\":true}");
        rejects([&]{(void)catalog.resolvePackage(first.file.key);},"changed manifest requires re-registration");
        const auto updated=catalog.registerPackage("Character.iiproject");
        check(updated.file.key==first.file.key&&updated.file.revision>first.file.revision&&updated.file.contentVersion!=first.file.contentVersion,"manifest change advances catalog revision");
        rejects([&]{(void)catalog.resolvePackage(updated.file.key,first.file.contentVersion);},"stale content version rejected");
        fs::rename(root/"Character.iiproject",root/"Moved.iiproject");fs::rename(root/"Character.iiproject.iisacc.json",root/"Moved.iiproject.iisacc.json");
        const auto moved=catalog.registerPackage("Moved.iiproject");check(moved.file.key==first.file.key&&catalog.itemKind(moved.file.key)==CatalogEntryKind::Package,"rename preserves catalog key");
        fs::copy(root/"Moved.iiproject",root/"Copy.iiproject",fs::copy_options::recursive);fs::copy_file(root/"Moved.iiproject.iisacc.json",root/"Copy.iiproject.iisacc.json");
        const auto copy=catalog.registerPackage("Copy.iiproject");check(copy.file.key!=moved.file.key&&copy.file.metadata==moved.file.metadata,"copied package receives independent catalog key");
        fs::create_directory(root/"Missing.iiproject");rejects([&]{(void)catalog.registerPackage("Missing.iiproject");},"manifest must exist");
        fs::create_directory(root/"NotAPackage");write(root/"NotAPackage/manifest.json","{}");rejects([&]{(void)catalog.registerPackage("NotAPackage");},"package extension required");
        fs::create_directories(root/"AlreadyIndexed.iiproject/assets");write(root/"AlreadyIndexed.iiproject/manifest.json","{}");write(root/"AlreadyIndexed.iiproject/assets/legacy.txt","keep");
        const auto legacyItem=catalog.registerFile("AlreadyIndexed.iiproject/assets/legacy.txt");
        rejects([&]{(void)catalog.registerPackage("AlreadyIndexed.iiproject");},"existing independently registered children are preserved");
        check(catalog.lookup(legacyItem.file.key).has_value(),"conflicting child classification survives");
        std::error_code linkError;fs::create_directory_symlink(root/"Moved.iiproject",root/"Redirect.iiproject",linkError);
        if(!linkError)rejects([&]{(void)catalog.registerPackage("Redirect.iiproject");},"package symlink rejected");
        write(root/"regular.txt","ordinary");const auto normal=catalog.registerFile("regular.txt");check(catalog.itemKind(normal.file.key)==CatalogEntryKind::File,"ordinary files keep their type");
        rejects([&]{(void)catalog.resolvePackage(normal.file.key);},"ordinary file is not a package");
        const auto updatedMetadata=catalog.updateMetadata(moved.file.key,moved.file.revision,moved.file.metadata);check(updatedMetadata.metadataWritten,"package classification can be updated");
    }
    {FileCatalog catalog(root,FileCatalog::Access::ReadOnly);auto entry=catalog.lookupPath("Moved.iiproject");check(entry&&catalog.itemKind(entry->key)==CatalogEntryKind::Package,"read-only reopen retains package kind");check(catalog.resolvePackage(entry->key)==root/"Moved.iiproject","read-only resolve works");}
    {
        const auto legacy=root/"legacy-catalog";fs::create_directory(legacy);write(legacy/"file.txt","original");
        {FileCatalog old(legacy);(void)old.registerFile("file.txt");}
        sqlite3* db=nullptr;check(sqlite3_open((legacy/".iisacc-file-provider/catalog.sqlite3").string().c_str(),&db)==SQLITE_OK,"open legacy fixture");
        check(sqlite3_exec(db,"DROP TABLE package_items",nullptr,nullptr,nullptr)==SQLITE_OK,"simulate old schema 1");sqlite3_close(db);
        {FileCatalog old(legacy,FileCatalog::Access::ReadOnly);auto rows=old.search();check(rows.files.size()==1&&old.itemKind(rows.files[0].key)==CatalogEntryKind::File,"legacy read-only catalog remains readable");}
        {FileCatalog migrated(legacy,FileCatalog::Access::ReadWrite);check(migrated.count()==1,"adding package table preserves old rows");}
    }
    fs::remove_all(root);std::cout<<"catalog package registration: passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
