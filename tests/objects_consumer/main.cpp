#include <ObjectStore.h>
#include <OntologyFileSystem.h>
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
            iiFileProvider::ObjectSessionContext context;
            context.application="ObjectsInstalledConsumer";context.applicationVersion="1.0";
            context.project="fixture-project";context.document="fixture-document";
            const auto session=store.beginSession({"https://iisacc.com","fixture","Fixture"},"test","installed SDK",context);
            iiFileProvider::RawMetadata raw;raw.originalName="source";
            raw.annotations.title="Installed original";
            const auto file=store.wrapRaw(root/"source","Files/example",raw,session.key);
            if(!store.validate(file.key,1) || store.scan().size()!=1 || store.index().size()!=1)return 2;
            const auto inventory=iiFileProvider::ObjectPackager::inventory({{"Tree",root,{"package"}}});
            if(inventory.entries.size()!=1 || !inventory.issues.empty())return 3;
            auto metadata=raw.annotations;metadata.rating=5;
            const auto edited=store.reviseMetadata(file.key,metadata,1,session.key);
            if(edited.version!=2 || edited.sha256!=file.sha256 || !store.diff(file.key,2).empty())return 7;
            const auto changes=store.metadataDiff(file.key,2);
            if(changes.size()!=1 || changes[0].pointer!="/rating" || changes[0].afterJson!="5")return 8;
            if(store.workSession(session.key).context!=context || store.sessions().size()!=1 || store.journal().size()!=2)return 9;
            if(store.rawEnvelope(file.key,1).metadata!=raw || store.rawEnvelope(file.key).metadata.annotations!=metadata)return 10;
            iiFileProvider::OntologyFileSystem ontology(store);
            iiFileProvider::OntologyQuery query;query.classKey="Asset";
            ontology.apply({iiFileProvider::OntologyClass{"Asset","Asset","",{}},
                iiFileProvider::OntologyClass{"Raw","Raw","",{"Asset"}},
                iiFileProvider::OntologyTypeAssignment{iiFileProvider::OntologyResource::file(file.key),"Raw"},
                iiFileProvider::OntologyDirectory{"Library/Raw","Raw originals",query}},0,session.key);
            if(ontology.listDirectory("Library/Raw").files.size()!=1 || ontology.queryFiles(query).files[0].key!=file.key)return 12;
            if(store.lookup(file.key)->version!=2 || !store.validate(file.key,2))return 13;
            store.extract(file.key,2,root/"restored");
            std::ifstream restored(root/"restored",std::ios::binary);
            const std::string payload{std::istreambuf_iterator<char>(restored),{}};
            if(payload!="installed package")return 11;
            restored.close();
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
            iiFileProvider::ObjectStore documents(root/"documents","installed-documents",true);
            const auto session=documents.beginSession({"local:test","editor","Editor"},"device","installed document");
            const auto captured=documents.importDocument(root/"source","files/source",{"source"},session.key);
            if(captured.file.sha256!=captured.original.record.sha256 || documents.original(captured.file.key).record.key!=captured.original.record.key)return 14;
            documents.endSession(session.key);
        }
        {
            iiFileProvider::ObjectStore reader(root/"package","installed-consumer",
                iiFileProvider::ObjectStore::Access::ReadOnly);
            const auto file=reader.lookupPath("Files/example");
            if(!file || !reader.validate(file->key,file->version) || reader.index().size()!=1)return 6;
        }
        fs::remove_all(root);std::cout<<"Installed Qt-free ontology filesystem, RAW, metadata diff, journal and session APIs verified\n";return 0;
    } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
