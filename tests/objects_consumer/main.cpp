#include <ObjectStore.h>
#include <ObjectPackager.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#ifdef QT_VERSION
#error ObjectStore must not include Qt headers
#endif
static_assert(__cplusplus >= 202302L);
int main() {
    namespace fs=std::filesystem;
    try {
        const auto root=fs::current_path()/("installed-object-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directory(root);
        {std::ofstream file(root/"source");file<<"installed package";}
        {
            const auto expected=iiFileProvider::ObjectSource::inspect(root/"source");
            iiFileProvider::ObjectSource snapshot(root/"source",root,{},expected);
            if(snapshot.identity()!=expected || !fs::exists(snapshot.path()))return 4;
        }
        {
            iiFileProvider::ObjectStore store(root/"package","installed-consumer",true,262144);
            const auto session=store.beginSession({"https://iisacc.com","fixture","Fixture"},"test","installed SDK");
            const auto file=store.importFile(root/"source","Files/example",session.key);
            if(!store.validate(file.key,1) || store.scan().size()!=1 || store.index().size()!=1)return 2;
            const auto inventory=iiFileProvider::ObjectPackager::inventory({{"Tree",root,{"package"}}});
            if(inventory.entries.size()!=1 || !inventory.issues.empty())return 3;
            store.endSession(session.key);
        }
        {
            // Existing callers keep the original constructor; a configured
            // writer must remain readable through the installed default API.
            iiFileProvider::ObjectStore store(root/"package","installed-consumer");
            const auto file=store.lookupPath("Files/example");
            if(!file || !store.validate(file->key,file->version) || store.index().size()!=1)return 5;
        }
        {
            iiFileProvider::ObjectStore reader(root/"package","installed-consumer",
                iiFileProvider::ObjectStore::Access::ReadOnly);
            const auto file=reader.lookupPath("Files/example");
            if(!file || !reader.validate(file->key,file->version) || reader.index().size()!=1)return 6;
        }
        fs::remove_all(root);std::cout<<"Installed Qt-free object package verified\n";return 0;
    } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
