#include "ObjectPackager.h"
#include "ObjectBatchRead.h"
#include "ObjectHash.h"
#include "ObjectSnapshotBatch.h"
#include <fstream>
#include <stdexcept>

namespace iiFileProvider {
namespace {
struct AuditCandidate {
    const ObjectPackagingEntry *entry;
    ObjectIndexEntry expected;
};
std::string streamingHash(const ObjectSource &source,std::stop_token cancellation){
    std::vector<char> inputBuffer(ObjectStore::chunkBytes);
    std::vector<std::uint8_t> bytes(ObjectStore::chunkBytes);
    std::ifstream input;input.rdbuf()->pubsetbuf(inputBuffer.data(),inputBuffer.size());
    input.open(source.path(),std::ios::binary);
    if(!input)throw std::runtime_error("cannot read audit snapshot");
    detail::ObjectHash hash;
    while(input){
        if(cancellation.stop_requested())throw std::runtime_error("source audit cancelled");
        input.read(reinterpret_cast<char *>(bytes.data()),bytes.size());
        hash.update(std::span(bytes.data(),static_cast<std::size_t>(input.gcount())));
    }
    if(input.bad() || !input.eof())throw std::runtime_error("cannot finish source audit");
    return hash.finish();
}
}
ObjectAuditReport ObjectPackager::audit(const ObjectInventory &inventory,std::stop_token cancellation,
    const std::function<void(const ObjectPackagingEntry &,const std::string &)> &progress) const {
    ObjectAuditReport report;report.issues=inventory.issues;
    if(!report.issues.empty())return report;
    const auto staging=stagingDirectory();
    const auto stopped=[&]{if(cancellation.stop_requested()){report.cancelled=true;return true;}return false;};
    const auto prepare=[&](const ObjectPackagingEntry &entry){
        const auto record=m_store.lookupPath(entry.logicalPath);
        if(!record || record->sourceStamp!=entry.identity.stamp || record->size!=entry.identity.size
            || record->path!=entry.logicalPath || record->indexKey<=0)
            throw std::runtime_error("missing or stale packaged object");
        // Retain only compact fields, never hundreds of full author documents.
        const auto &r=*record;
        return AuditCandidate{&entry,{r.key,r.indexKey,r.version,r.path,r.sha256,r.size,
            r.validationKey,r.sessionKey,r.deleted,r.author.origin,r.author.subject}};
    };
    const auto validate=[&](const AuditCandidate &candidate,const std::string &hash){
        const auto &expected=candidate.expected;const auto &entry=*candidate.entry;
        if(hash!=expected.sha256 || !m_store.validate(expected.key,expected.version)
            || ObjectSource::inspect(entry.source)!=entry.identity)
            throw std::runtime_error("changed source or corrupt packaged object");
        const auto head=m_store.lookup(expected.key);
        if(!head || head->version!=expected.version || head->validationKey!=expected.validationKey
            || head->path!=expected.path || head->deleted)
            throw std::runtime_error("object head changed during audit");
        const auto index=m_store.index(expected.indexKey-1,1);
        if(index.size()!=1 || index.front()!=expected)
            throw std::runtime_error("compact object index differs from current head");
    };
    const auto publish=[&](const AuditCandidate &candidate){
        ++report.verified;if(progress)progress(*candidate.entry,candidate.expected.key);
    };
    const auto single=[&](const AuditCandidate &candidate){
        try{
            ObjectSource snapshot(candidate.entry->source,staging,cancellation,candidate.entry->identity);
            validate(candidate,streamingHash(snapshot,cancellation));
        }catch(const std::exception &error){
            if(!stopped())report.issues.push_back({candidate.entry->logicalPath,error.what()});
            return;
        }
        publish(candidate);
    };
    for(std::size_t cursor=0;cursor<inventory.entries.size();){
        if(stopped())return report;
        if(inventory.entries[cursor].identity.size>ObjectStore::maximumBatchBytes){
            const auto &entry=inventory.entries[cursor++];
            std::optional<AuditCandidate> candidate;
            try{candidate=prepare(entry);}catch(const std::exception &error){report.issues.push_back({entry.logicalPath,error.what()});}
            if(candidate)single(*candidate);
            continue;
        }
        std::vector<AuditCandidate> candidates;
        std::vector<detail::ObjectSnapshotRequest> requests;
        auto remaining=ObjectStore::maximumBatchBytes;
        while(cursor<inventory.entries.size() && candidates.size()<ObjectStore::maximumBatchFiles){
            if(stopped())return report;
            const auto &entry=inventory.entries[cursor];
            if(entry.identity.size>remaining)break;
            ++cursor;
            try{
                const auto candidate=prepare(entry);
                candidates.push_back(candidate);requests.push_back({entry.source,entry.identity});remaining-=entry.identity.size;
            }catch(const std::exception &error){report.issues.push_back({entry.logicalPath,error.what()});}
        }
        if(candidates.empty())continue;
        std::unique_ptr<detail::ObjectSnapshotBatch> snapshots;
        std::vector<detail::PreparedObjectSource> prepared;
        try{
            snapshots=std::make_unique<detail::ObjectSnapshotBatch>(requests,staging,cancellation);
            std::vector<ObjectImport> inputs;inputs.reserve(candidates.size());
            for(std::size_t i=0;i<candidates.size();++i){ObjectWriteOptions options;options.cancellation=cancellation;
                inputs.push_back({snapshots->path(i),candidates[i].entry->logicalPath,std::move(options)});}
            prepared=detail::readObjectBatch(inputs);
        }catch(const std::exception &){
            snapshots.reset();
            if(stopped())return report;
            // Isolate a failed member: do not falsely label its healthy neighbors
            // as corrupt, and never omit a member of the mapped inventory.
            for(const auto &candidate:candidates){if(stopped())return report;single(candidate);}
            continue;
        }
        for(std::size_t i=0;i<candidates.size();++i){
            if(stopped())return report;
            try{validate(candidates[i],detail::ObjectHash::digest(prepared[i].bytes));}
            catch(const std::exception &error){report.issues.push_back({candidates[i].entry->logicalPath,error.what()});continue;}
            publish(candidates[i]);
        }
    }
    if(stopped())return report;
    try{
        std::uint64_t indexed=0;std::int64_t cursor=0;
        for(;;){if(stopped())return report;const auto page=m_store.index(cursor,1024);if(page.empty())break;
            indexed+=page.size();cursor=page.back().indexKey;}
        if(m_store.count()!=inventory.entries.size() || indexed!=inventory.entries.size())
            report.issues.push_back({"","object/index count differs from mapped tree"});
    }catch(const std::exception &error){report.issues.push_back({"",error.what()});}
    return report;
}
}
