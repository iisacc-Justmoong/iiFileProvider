#include "ObjectSnapshotBatch.h"
#include "ObjectStore.h"
#include <algorithm>
#include <atomic>
#include <exception>
#include <stdexcept>
#include <thread>

namespace iiFileProvider::detail {
ObjectSnapshotBatch::ObjectSnapshotBatch(const std::vector<ObjectSnapshotRequest> &requests,
    const std::filesystem::path &staging,std::stop_token cancellation,std::size_t concurrency) {
    if(requests.empty() || requests.size()>ObjectStore::maximumBatchFiles)
        throw std::runtime_error("invalid snapshot batch count");
    if(cancellation.stop_requested())throw std::runtime_error("object snapshot cancelled");
    auto remaining=ObjectStore::maximumBatchBytes;
    for(const auto &request:requests){
        if(request.identity.size>remaining)throw std::runtime_error("snapshot batch exceeds 16 MiB");
        remaining-=request.identity.size;
    }
    m_concurrency=std::min({requests.size(),std::size_t(64),concurrency?concurrency:std::size_t(std::max(1u,std::thread::hardware_concurrency()))});
    m_sources.resize(requests.size());
    try {
        std::atomic_size_t cursor=0;
        std::atomic_bool failed=false;
        std::vector<std::exception_ptr> errors(requests.size());
        {
            std::vector<std::jthread> workers;workers.reserve(m_concurrency);
            for(std::size_t worker=0;worker<m_concurrency;++worker)workers.emplace_back([&]{
                while(!failed.load()){
                    const auto index=cursor.fetch_add(1);
                    if(index>=requests.size())return;
                    try{
                        const auto &request=requests[index];
                        // Verify before allocation/copy and again against the
                        // acquired descriptor to reject a changed inventory.
                        if(cancellation.stop_requested())throw std::runtime_error("object snapshot cancelled");
                        if(ObjectSource::inspect(request.source)!=request.identity)
                            throw std::runtime_error("source changed before snapshot; rescan required");
                        auto source=std::make_unique<ObjectSource>(request.source,staging,cancellation,request.identity);
                        if(source->identity()!=request.identity)
                            throw std::runtime_error("source changed before snapshot; rescan required");
                        m_sources[index]=std::move(source);
                    }catch(...){errors[index]=std::current_exception();failed.store(true);}
                }
            });
        } // Join every worker, including when creating a later worker throws.
        for(const auto &error:errors)if(error)std::rethrow_exception(error);
        if(cancellation.stop_requested())throw std::runtime_error("object snapshot cancelled");
    }catch(...){clear();throw;}
}
ObjectSnapshotBatch::~ObjectSnapshotBatch(){clear();}
const std::filesystem::path &ObjectSnapshotBatch::path(std::size_t index) const{return m_sources.at(index)->path();}
void ObjectSnapshotBatch::clear() noexcept {
    // Every index has exactly one owner. Failure to allocate/start cleanup
    // workers falls back to the caller; already-started workers still join.
    std::atomic_size_t cursor=0;
    const auto clean=[&]{for(;;){const auto index=cursor.fetch_add(1);if(index>=m_sources.size())return;m_sources[index].reset();}};
    {
        std::vector<std::jthread> workers;
        try{
            workers.reserve(m_concurrency-1);
            for(std::size_t worker=1;worker<m_concurrency;++worker)workers.emplace_back(clean);
        }catch(...){/* The caller drains all unclaimed indices. */}
        clean();
    }
    m_sources.clear();
}
}
