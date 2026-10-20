#include "ObjectSource.h"
#include "ObjectSnapshotBatch.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs=std::filesystem;
int main() {
    try {
        const auto root=fs::path(OBJECT_TEST_DIRECTORY)/("snapshot-identity-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root/"staging");
        struct Cleanup {fs::path root;~Cleanup(){std::error_code error;fs::remove_all(root,error);}} cleanup{root};
        std::vector<iiFileProvider::detail::ObjectSnapshotRequest> requests;
        for (int i=0;i<12;++i) {
            const auto path=root/std::to_string(i);
            {std::ofstream output(path,std::ios::binary);output<<"snapshot-payload-"<<i;}
            requests.push_back({path,iiFileProvider::ObjectSource::inspect(path)});
        }
        {
            iiFileProvider::detail::ObjectSnapshotBatch snapshots(requests,root/"staging",{},4);
            for (std::size_t i=0;i<requests.size();++i) {
                std::ifstream input(snapshots.path(i),std::ios::binary);
                std::string actual((std::istreambuf_iterator<char>(input)),{});
                if (actual!="snapshot-payload-"+std::to_string(i)) throw std::runtime_error("snapshot bytes differ");
            }
        }
        for (const auto& request:requests) {
            if (iiFileProvider::ObjectSource::inspect(request.source)!=request.identity)
                throw std::runtime_error("snapshot retirement changed original source identity");
        }
        // Metadata sensitivity is retained: a real source write must invalidate it.
        {std::ofstream output(requests[0].source,std::ios::binary|std::ios::app);output<<"changed";}
        bool rejected=false;
        try {iiFileProvider::ObjectSource snapshot(requests[0].source,root/"staging",{},requests[0].identity);}
        catch (const std::exception&) {rejected=true;}
        if (!rejected) throw std::runtime_error("changed source was accepted");
        return 0;
    } catch (const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
