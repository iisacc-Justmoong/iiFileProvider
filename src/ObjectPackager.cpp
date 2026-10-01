#include "ObjectPackager.h"
#include "ObjectSnapshotBatch.h"
#include <algorithm>
#include <atomic>
#include <limits>
#include <set>
#include <stdexcept>
#include <thread>

namespace iiFileProvider {
namespace fs=std::filesystem;
namespace {
std::string utf8(const fs::path &p){const auto value=p.generic_u8string();return {reinterpret_cast<const char *>(value.data()),value.size()};}
bool below(const fs::path &child,const fs::path &parent){const auto p=child.lexically_relative(parent);return !p.empty() && *p.begin()!="..";}
void nameValid(const std::string &name){
    if(name.empty() || name=="." || name==".." || name.find_first_of("/\\:")!=std::string::npos || name.find('\0')!=std::string::npos)
        throw std::runtime_error("invalid object-tree namespace");
}
}
ObjectInventory ObjectPackager::inventory(const std::vector<ObjectTreeMapping> &mappings,std::stop_token cancellation,std::size_t concurrency){
    ObjectInventory result;std::set<std::string> prefixes;std::vector<fs::path> roots;
    if(mappings.empty())throw std::runtime_error("at least one object tree is required");
    for(const auto &mapping:mappings){
        nameValid(mapping.prefix);if(!prefixes.insert(mapping.prefix).second)throw std::runtime_error("duplicate logical tree");
        for(const auto &excluded:mapping.excludedTopLevel)nameValid(excluded);
        const auto root=fs::absolute(mapping.root).lexically_normal();
        if(!fs::is_directory(root) || fs::canonical(root)!=root)throw std::runtime_error("unavailable or redirected object tree");
        for(const auto &other:roots)if(below(root,other) || below(other,root))throw std::runtime_error("overlapping object trees");roots.push_back(root);
        std::error_code failure;
        fs::recursive_directory_iterator it(root,fs::directory_options::none,failure),end;
        if(failure){result.issues.push_back({mapping.prefix,failure.message()});continue;}
        while(it!=end){
            if(cancellation.stop_requested())throw std::runtime_error("object inventory cancelled");
            const auto path=it->path();const auto relative=path.lexically_relative(root);const auto first=utf8(*relative.begin());
            if(std::find(mapping.excludedTopLevel.begin(),mapping.excludedTopLevel.end(),first)!=mapping.excludedTopLevel.end())it.disable_recursion_pending();
            else {
                const auto logical=mapping.prefix+"/"+utf8(relative);
                try{
                    const auto type=it->symlink_status();
                    if(fs::is_symlink(type)){it.disable_recursion_pending();throw std::runtime_error("symbolic link is not a package payload");}
                    if(fs::is_regular_file(type)){
                        result.entries.push_back({logical,path,{}});
                    }else if(!fs::is_directory(type))throw std::runtime_error("unsupported special file");
                }catch(const std::exception &e){result.issues.push_back({logical,e.what()});}
            }
            it.increment(failure);if(failure){result.issues.push_back({mapping.prefix,failure.message()});break;}
        }
    }
    // Native metadata waits are independent. Bound simultaneous handles while
    // using all available CPU workers by default; traversal/order stay stable.
    const auto count=std::min(result.entries.size(),std::clamp<std::size_t>(
        concurrency?concurrency:std::thread::hardware_concurrency(),1,64));
    std::atomic_size_t next=0;std::vector<std::string> failures(result.entries.size());
    {
        std::vector<std::jthread> workers;workers.reserve(count);
        for(std::size_t worker=0;worker<count;++worker)workers.emplace_back([&]{
            while(!cancellation.stop_requested()){
                const auto index=next.fetch_add(1,std::memory_order_relaxed);
                if(index>=result.entries.size())break;
                try{result.entries[index].identity=ObjectSource::inspect(result.entries[index].source);}
                catch(const std::exception &e){failures[index]=e.what();}
            }
        });
    }
    if(cancellation.stop_requested())throw std::runtime_error("object inventory cancelled");
    for(std::size_t i=0;i<result.entries.size();++i){
        if(!failures[i].empty()){result.issues.push_back({result.entries[i].logicalPath,failures[i]});continue;}
        const auto size=result.entries[i].identity.size;
        if(size>std::numeric_limits<std::uint64_t>::max()-result.totalBytes)throw std::runtime_error("inventory size overflow");
        result.totalBytes+=size;
    }
    std::sort(result.entries.begin(),result.entries.end(),[](const auto &a,const auto &b){return a.logicalPath<b.logicalPath;});return result;
}
ObjectPackager::ObjectPackager(ObjectStore &store):m_store(store),m_directory(store.directory()){
    if(fs::canonical(m_directory)!=m_directory || !fs::is_directory(m_directory))throw std::runtime_error("invalid package directory");
}
fs::path ObjectPackager::stagingDirectory() const {
    const auto staging=m_directory/"staging";
    if(!fs::exists(staging))fs::create_directory(staging);
    if(fs::canonical(staging)!=staging || !fs::is_directory(staging))throw std::runtime_error("redirected package staging path");
    fs::permissions(staging,fs::perms::owner_all,fs::perm_options::replace);
    return staging;
}
ObjectPackagingReport ObjectPackager::package(const ObjectInventory &inventory,const std::string &session,std::stop_token cancellation,
    const std::function<void(const ObjectPackagingEntry &,const ObjectRecord &,const char *)> &progress,
    const std::function<std::optional<ObjectMetadata>(const ObjectPackagingEntry &)> &authorship,std::size_t maximumBatchFiles){
    if(!maximumBatchFiles || maximumBatchFiles>ObjectStore::maximumBatchFiles)throw std::runtime_error("invalid package batch size");
    ObjectPackagingReport result;result.issues=inventory.issues;
    // An incomplete inventory must never look like a complete drive migration.
    if(!result.issues.empty())return result;
    const auto staging=stagingDirectory();
    const auto work=m_store.session(session);if(work.endedAtNs)throw std::runtime_error("package work session is closed");
    for(std::size_t cursor=0;cursor<inventory.entries.size();){
        if(cancellation.stop_requested()){result.cancelled=true;break;}
        const auto &entry=inventory.entries[cursor++];
        try{
            if(below(entry.source,m_directory))throw std::runtime_error("cannot package the package itself");
            const auto current=ObjectSource::inspect(entry.source);
            if(current!=entry.identity)throw std::runtime_error("source changed after inventory; rescan required");
            const auto existing=m_store.lookupPath(entry.logicalPath);
            // Provenance may become available independently of the source bytes.
            // Omission preserves it; an explicit empty document clears it.
            const std::optional<ObjectMetadata> suppliedAuthorship=existing && authorship?authorship(entry):std::nullopt;
            if(existing && existing->sourceStamp==current.stamp
                && (!suppliedAuthorship || *suppliedAuthorship==existing->authorship)){
                if(!m_store.validate(existing->key,existing->version,false))throw std::runtime_error("committed object metadata is corrupt");
                ++result.skipped;if(progress)progress(entry,*existing,"skipped");continue;
            }
            constexpr std::uint64_t reserve=256ull*1024*1024;
            const auto available=fs::space(m_directory).available;
            if(current.size>(std::numeric_limits<std::uint64_t>::max()-reserve)/3 || available<current.size*3+reserve)
                throw std::runtime_error("insufficient free space for snapshot, payload and journal");
            if(!existing && maximumBatchFiles>1 && current.size<=ObjectStore::maximumBatchBytes){
                std::vector<detail::ObjectSnapshotRequest> requests;
                std::vector<ObjectImport> imports;
                std::vector<const ObjectPackagingEntry *> entries;
                requests.reserve(maximumBatchFiles);imports.reserve(maximumBatchFiles);entries.reserve(maximumBatchFiles);
                std::uint64_t bytes=0;
                std::size_t metadataRemaining=ObjectStore::maximumBatchMetadataBytes;
                const auto prepare=[&](const ObjectPackagingEntry &item){
                    if(below(item.source,m_directory))throw std::runtime_error("cannot package the package itself");
                    const auto identity=ObjectSource::inspect(item.source);
                    if(identity!=item.identity)throw std::runtime_error("source changed after inventory; rescan required");
                    if(identity.size>ObjectStore::maximumBatchBytes-bytes)throw std::runtime_error("package batch size limit");
                    if(fs::space(m_directory).available<(bytes+identity.size)*3+reserve)throw std::runtime_error("insufficient batch space");
                    ObjectWriteOptions options;options.sourceStamp=identity.stamp;options.cancellation=cancellation;
                    if(authorship)options.authorship=authorship(item);
                    auto remaining=metadataRemaining;
                    const auto actorBytes=work.author.profile.payload.size();
                    const auto provenanceBytes=options.authorship?options.authorship->payload.size():0;
                    if(actorBytes>remaining)throw std::runtime_error("package batch metadata limit");
                    remaining-=actorBytes;
                    if(provenanceBytes>remaining)throw std::runtime_error("package batch metadata limit");
                    remaining-=provenanceBytes;
                    imports.push_back({item.source,item.logicalPath,std::move(options)});
                    requests.push_back({item.source,identity});entries.push_back(&item);bytes+=identity.size;
                    metadataRemaining=remaining;
                };
                prepare(entry);
                while(cursor<inventory.entries.size() && imports.size()<maximumBatchFiles){
                    const auto &next=inventory.entries[cursor];
                    if(next.identity.size>ObjectStore::maximumBatchBytes-bytes)break;
                    try{
                        if(m_store.lookupPath(next.logicalPath))break;
                        prepare(next);++cursor;
                    }catch(const std::exception &){
                        // Publish the already prepared prefix; retry this next
                        // item through the ordinary per-item error path.
                        break;
                    }
                }
                std::vector<ObjectRecord> records;
                std::unique_ptr<detail::ObjectSnapshotBatch> snapshots;
                try{
                    snapshots=std::make_unique<detail::ObjectSnapshotBatch>(requests,staging,cancellation);
                    for(std::size_t i=0;i<imports.size();++i)imports[i].source=snapshots->path(i);
                    records=m_store.importFiles(imports,session);
                }
                catch(const std::exception &e){
                    if(cancellation.stop_requested())throw;
                    for(const auto *item:entries)result.issues.push_back({item->logicalPath,std::string("batch rolled back: ")+e.what()});
                    continue;
                }
                result.imported+=records.size();
                for(std::size_t i=0;i<records.size();++i)if(progress)progress(*entries[i],records[i],"imported");
                continue;
            }
            ObjectSource snapshot(entry.source,staging,cancellation);
            if(snapshot.identity()!=current)throw std::runtime_error("source changed before snapshot; rescan required");
            ObjectWriteOptions options;options.sourceStamp=current.stamp;options.cancellation=cancellation;
            if(existing)options.authorship=suppliedAuthorship;
            else if(authorship)options.authorship=authorship(entry);
            const auto object=existing?m_store.reviseFile(existing->key,snapshot.path(),existing->version,session,options)
                                      :m_store.importFile(snapshot.path(),entry.logicalPath,session,options);
            if(existing)++result.revised;else ++result.imported;
            if(progress)progress(entry,object,existing?"revised":"imported");
        }catch(const std::exception &e){
            if(cancellation.stop_requested()){result.cancelled=true;break;}
            result.issues.push_back({entry.logicalPath,e.what()});
        }
    }
    return result;
}
}
