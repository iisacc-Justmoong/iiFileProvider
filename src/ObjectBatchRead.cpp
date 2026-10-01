#include "ObjectBatchRead.h"
#include <algorithm>
#include <atomic>
#include <exception>
#include <fstream>
#include <stdexcept>
#include <thread>

namespace iiFileProvider::detail {
std::vector<PreparedObjectSource> readObjectBatch(const std::vector<ObjectImport> &files,std::size_t concurrency) {
    if(files.empty() || files.size()>ObjectStore::maximumBatchFiles)
        throw std::runtime_error("invalid object batch read count");
    std::vector<PreparedObjectSource> prepared(files.size());
    std::uint64_t remaining=ObjectStore::maximumBatchBytes;
    // Inspect every size before allocating/reading any payload. Workers cannot
    // exceed the aggregate budget by racing separate size checks.
    for(std::size_t i=0;i<files.size();++i){
        if(files[i].options.cancellation.stop_requested())throw std::runtime_error("object packaging cancelled");
        auto &item=prepared[i];
        item.source=std::filesystem::absolute(files[i].source).lexically_normal();
        item.identity=ObjectSource::inspect(item.source);
        if(item.identity.size>remaining)throw std::runtime_error("object batch exceeds 16 MiB");
        remaining-=item.identity.size;
    }
    if(!concurrency)concurrency=std::max(1u,std::thread::hardware_concurrency());
    concurrency=std::min({concurrency,files.size(),std::size_t(64)});
    std::atomic_size_t cursor=0;
    std::atomic_bool failed=false;
    std::vector<std::exception_ptr> errors(files.size());
    std::vector<std::jthread> workers;
    workers.reserve(concurrency);
    for(std::size_t worker=0;worker<concurrency;++worker)workers.emplace_back([&]{
        while(!failed.load()){
            const auto index=cursor.fetch_add(1);
            if(index>=files.size())return;
            try{
                const auto cancellation=files[index].options.cancellation;
                if(cancellation.stop_requested())throw std::runtime_error("object packaging cancelled");
                auto &item=prepared[index];
                std::vector<char> buffer(std::max<std::size_t>(1,std::min<std::uint64_t>(item.identity.size,ObjectStore::chunkBytes)));
                std::ifstream input;input.rdbuf()->pubsetbuf(buffer.data(),buffer.size());
                input.open(item.source,std::ios::binary);
                if(!input)throw std::runtime_error("cannot read object batch source");
                item.bytes.resize(static_cast<std::size_t>(item.identity.size));
                for(std::size_t offset=0;offset<item.bytes.size();){
                    if(cancellation.stop_requested())throw std::runtime_error("object packaging cancelled");
                    const auto length=std::min(ObjectStore::chunkBytes,item.bytes.size()-offset);
                    input.read(reinterpret_cast<char *>(item.bytes.data()+offset),length);
                    if(input.bad() || static_cast<std::size_t>(input.gcount())!=length)
                        throw std::runtime_error("object batch source changed or read failed");
                    offset+=length;
                }
                if(cancellation.stop_requested())throw std::runtime_error("object packaging cancelled");
                if(ObjectSource::inspect(item.source)!=item.identity)
                    throw std::runtime_error("object batch source changed during read");
            }catch(...){errors[index]=std::current_exception();failed.store(true);}
        }
    });
    // Explicitly join before examining errors or moving the buffers. jthread
    // also joins during unwinding if allocating/starting a later worker fails.
    workers.clear();
    for(const auto &error:errors)if(error)std::rethrow_exception(error);
    return prepared;
}
}
