#include "ObjectPackager.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs=std::filesystem;
using namespace iiFileProvider;
void check(bool condition,const char *message){if(!condition)throw std::runtime_error(message);}
int main(){try{
    const auto root=fs::current_path()/("packager-metadata-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root/"source");
    {std::ofstream source(root/"source/document");source<<"unchanged content";}
    {
        ObjectStore store(root/"package","metadata-refresh",true);
        ObjectPackager packager(store);
        const auto session=store.beginSession({"local:test","packager","Recording actor"},"test","provenance reconciliation");
        const auto inventory=ObjectPackager::inventory({{"Files",root/"source",{}}});
        check(packager.package(inventory,session.key).imported==1,"initial unknown provenance import");
        const auto original=store.lookupPath("Files/document").value();
        std::optional<ObjectMetadata> metadata=ObjectMetadata{"iiFileProvider.Authorship/3",
            R"({"schemaVersion":3,"authors":[],"participants":[],"links":[],"revision":1})"};
        const auto supplied=[&](const ObjectPackagingEntry &){return metadata;};
        const auto learned=packager.package(inventory,session.key,{}, {},supplied);
        check(learned.revised==1 && learned.skipped==0 && learned.issues.empty(),"unchanged bytes must reconcile newly supplied provenance");
        const auto enriched=store.lookup(original.key).value();
        check(enriched.indexKey==original.indexKey && enriched.version==2 && enriched.sha256==original.sha256
              && enriched.sourceStamp==original.sourceStamp && enriched.authorship==*metadata,
              "metadata revision retains identity, bytes and exact document");
        check(enriched.validationKey!=original.validationKey && store.diff(original.key,2).empty()
              && store.validate(original.key,2),"metadata-only revision is journaled without payload diff");
        const auto repeated=packager.package(inventory,session.key,{}, {},supplied);
        check(repeated.skipped==1 && repeated.revised==0,"identical supplied provenance is idempotent");
        metadata.reset();
        check(packager.package(inventory,session.key,{}, {},supplied).skipped==1
              && store.lookup(original.key)->authorship==enriched.authorship,"omitted provenance preserves known document");
        check(packager.package(inventory,session.key).skipped==1
              && store.lookup(original.key)->version==2,"no metadata reader preserves the original resume behavior");
        metadata=ObjectMetadata{};
        check(packager.package(inventory,session.key,{}, {},supplied).revised==1,"explicit empty provenance records a clearing revision");
        const auto cleared=store.lookup(original.key).value();
        check(cleared.version==3 && cleared.authorship.payload.empty() && cleared.sha256==original.sha256,
              "explicit clearing does not change content");
        check(packager.package(inventory,session.key,{}, {},supplied).skipped==1,
              "repeated explicit clearing is idempotent");
        metadata=ObjectMetadata{"iiFileProvider.Authorship/3","invalid-json"};
        const auto invalid=packager.package(inventory,session.key,{}, {},supplied);
        check(invalid.issues.size()==1 && invalid.revised==0 && store.lookup(original.key)->version==3,
              "invalid newly supplied metadata must not be skipped or published");
        check(store.history(original.key).size()==3 && store.history(original.key)[1].authorship==enriched.authorship,
              "previous provenance remains in immutable history");
        check(fs::is_empty(root/"package/staging"),"metadata reconciliation snapshots cleaned");
        store.endSession(session.key);
    }
    fs::remove_all(root);
    std::cout<<"Packager provenance reconciliation passed\n";
    return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
