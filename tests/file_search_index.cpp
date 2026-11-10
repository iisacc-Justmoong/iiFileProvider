#include "FileSearchIndex.h"
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while(false)
int main() {
    using namespace iiFileProvider;
    FileSearchIndex index;
    FileSearchEntry classified{.path="folder/one.txt", .title="Café", .fileKey="key-1",
        .classification="{metadata}", .searchTerms="Café portrait", .size=9, .hasClassification=true, .metadataPending=true};
    CHECK(index.upsert(classified));
    FileSearchEntry presentation{.path="folder/one.txt", .title="one.txt", .mimeType="text/plain",
        .searchTerms="travel poster", .size=9, .hasPresentation=true};
    CHECK(!index.upsert(presentation));
    CHECK(index.contains("folder/one.txt"));
    const auto results=index.search("CAFE portrait",20);
    CHECK(results.size()==1 && results[0].title=="Café");
    CHECK(results[0].mimeType=="text/plain" && results[0].fileKey=="key-1");
    CHECK(results[0].hasClassification && results[0].hasPresentation && results[0].metadataPending);
    CHECK(index.search("travel",20).size()==1);
    CHECK(index.search("travel OR poster",20).empty());
    const auto found = index.find("folder/one.txt");
    CHECK(found && found->fileKey == "key-1" && found->mimeType == "text/plain");
    CHECK(!index.find("missing.txt"));
    for (int i = 0; i < 1100; ++i) {
        std::ostringstream path; path << "bulk/" << std::setw(4) << std::setfill('0') << i;
        FileSearchEntry entry{.path=path.str(), .title="Bulk", .fileKey=path.str(), .searchTerms="bulk target"};
        CHECK(index.upsert(entry));
    }
    std::string after;
    std::size_t count=0;
    for (;;) {
        const auto page=index.searchPage("bulk",after,127);
        if (page.empty()) break;
        for (const auto& entry : page) { CHECK(entry.path > after); after=entry.path; ++count; }
    }
    CHECK(count==1100 && after=="bulk/1099");
    CHECK(index.searchPage("bulk","bulk/1098",10).size()==1);
    CHECK(index.searchPage("bulk OR target","",10).empty());
    FileSearchEntry displayFirst{.path="second.txt", .title="display", .mimeType="text/plain", .searchTerms="display", .hasPresentation=true};
    FileSearchEntry classificationSecond{.path="second.txt", .title="classified", .fileKey="key-2", .classification="{}", .searchTerms="classified", .hasClassification=true, .metadataPending=true};
    CHECK(index.upsert(displayFirst) && !index.upsert(classificationSecond));
    const auto second = index.search("classified");
    CHECK(second.size()==1 && second[0].title=="classified" && second[0].hasPresentation && second[0].metadataPending);
    bool bad=false; try { (void)index.search("x",0); } catch(const std::invalid_argument&) { bad=true; } CHECK(bad);
    bad=false; try { (void)index.search(std::string(4097,'a'),1); } catch(const std::invalid_argument&) { bad=true; } CHECK(bad);
    bad=false; try { (void)index.search(std::string_view("bad\0query",9)); } catch(const std::invalid_argument&) { bad=true; } CHECK(bad);
    bad=false; try { (void)index.searchPage("",std::string_view("bad\0path",8),1); } catch(const std::invalid_argument&) { bad=true; } CHECK(bad);
    bad=false; try { FileSearchEntry e; e.path=std::string("bad\0path",8); (void)index.upsert(e); } catch(const std::invalid_argument&) { bad=true; } CHECK(bad);
    std::vector<std::thread> workers;
    for(int i=0;i<4;++i) workers.emplace_back([&,i] {
        for(int j=0;j<25;++j) { FileSearchEntry e; e.path="thread/"+std::to_string(i)+"/"+std::to_string(j);
            e.title="parallel"; e.searchTerms="parallel"; (void)index.upsert(e); }
    });
    for(auto& worker:workers) worker.join();
    CHECK(index.search("parallel",200).size()==100);
    FileSearchIndex empty; CHECK(empty.search("",20).empty());
}
