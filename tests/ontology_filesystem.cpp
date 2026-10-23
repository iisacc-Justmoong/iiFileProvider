#include "OntologyFileSystem.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace iiFileProvider;
namespace fs = std::filesystem;
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
template<class F> void rejects(F f) {
    bool rejected = false; try { f(); } catch (const std::exception &) { rejected = true; }
    require(rejected, "invalid ontology operation must reject");
}
int main() try {
    const auto root = fs::path(OBJECT_TEST_DIRECTORY)/("ontology-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    { std::ofstream out(root/"original.raw", std::ios::binary); out.write("original bytes\0data",19); }
    std::string firstKey, sessionKey;
    {
        ObjectStore store(root/"package", "ontology-test", true);
        auto session = store.beginSession({"local:test", "editor", "Editor"}, "device", "ontology");
        sessionKey = session.key;
        RawMetadata raw; raw.originalName = "original.raw";
        const auto first = store.wrapRaw(root/"original.raw", "Files/first.raw", raw, session.key);
        firstKey = first.key;
        const auto second = store.importFile(root/"original.raw", "Files/second", session.key);
        OntologyFileSystem ontology(store);
        const auto file = OntologyResource::file(first.key);
        const auto other = OntologyResource::file(second.key);
        const auto project = OntologyResource::entity("project:one");
        ontology.apply({
            OntologyClass{"Asset", "Asset", "", {}},
            OntologyClass{"Named", "Named", "", {"Asset"}},
            OntologyClass{"Photo", "Photo", "", {"Asset","Named"}},
            OntologyClass{"Project", "Project", "", {}},
            OntologyProperty{"project", "Project", OntologyValueKind::Resource, "Asset", "Project", true},
            OntologyProperty{"rating", "Rating", OntologyValueKind::Integer, "Asset", "", true},
            OntologyProperty{"derived", "Derived from", OntologyValueKind::Resource, "Asset", "Asset", false, true},
            OntologyEntity{"project:one", "First project", ""},
            OntologyTypeAssignment{project, "Project"},
            OntologyTypeAssignment{file, "Photo"},
            OntologyTypeAssignment{other, "Asset"},
            OntologyAssertion{file, "project", OntologyValue::resource(project)},
            OntologyAssertion{file, "rating", OntologyValue::integer(5)},
            OntologyAssertion{other, "derived", OntologyValue::resource(file)}
        }, 0, session.key);
        require(ontology.revision() == 1, "one ontology revision per atomic batch");
        OntologyQuery query; query.classKey = "Asset";
        auto page = ontology.queryFiles(query);
        require(page.files.size() == 2, "subclass inference includes Photo instances");
        require(ontology.queryFiles({"Named"}).files.size()==1 && ontology.ontologyClass("Photo")->parents.size()==2,"multiple inheritance and diamond closure retain one file result");
        query.includeSubclasses = false;
        require(ontology.queryFiles(query).files.size() == 1, "exact class query");
        query = {}; query.classKey = "Photo";
        query.conditions = {{"project", OntologyValue::resource(project)}, {"rating", OntologyValue::integer(5)}};
        require(ontology.queryFiles(query).files[0].key == first.key, "indexed class and typed relation query");
        ontology.apply({OntologyDirectory{"Projects/First", "First project photos", query}}, 1, session.key);
        require(ontology.listDirectory("Projects/First").files[0].key == first.key, "persistent semantic directory");
        require(ontology.directories("Projects/").size() == 1, "directory prefix listing");
        auto walks = ontology.traverse(other, "derived");
        require(walks.nodes.size() == 1 && walks.nodes[0].resource == file && walks.nodes[0].depth == 1, "forward relation traversal");
        require(ontology.traverse(file, "derived", OntologyDirection::Incoming).nodes[0].resource == other, "reverse relation traversal");
        const auto revision = ontology.revision();
        rejects([&] { ontology.apply({OntologyAssertion{file, "derived", OntologyValue::resource(other)}}, revision, session.key); });
        require(ontology.revision() == revision, "cyclic relation batch rolls back");
        rejects([&] { ontology.apply({OntologyTypeAssignment{project, "Project", false}}, revision, session.key); });
        rejects([&] { ontology.apply({OntologyClass{"Asset", "Asset", "", {"Photo"}}}, revision, session.key); });
        rejects([&] { ontology.apply({OntologyAssertion{file, "rating", OntologyValue::text("five")}}, revision, session.key); });
        rejects([&] { ontology.apply({OntologyAssertion{file, "rating", OntologyValue::integer(4)}}, revision, session.key); });
        require(ontology.revision() == revision && ontology.queryFiles(query).files.size() == 1, "failed domain/type/cardinality changes are atomic");
        auto cursorPage = ontology.queryFiles({}, {}, 1);
        require(cursorPage.next.has_value(), "semantic file pagination");
        auto next = ontology.queryFiles({}, *cursorPage.next, 1);
        require(next.files.size() == 1 && next.files[0].key != cursorPage.files[0].key, "keyset page has no duplicates");
        rejects([&] { ontology.queryFiles(query, *cursorPage.next); });
        store.move(first.key, "Moved/first.raw", 1, session.key);
        rejects([&] { ontology.queryFiles({}, *cursorPage.next); });
        require(ontology.listDirectory("Projects/First").files[0].path == "Moved/first.raw", "file move retains graph identity and updates directory path");
        ontology.extractFile(first.key, 2, root/"restored.raw");
        require(fs::file_size(root/"restored.raw") == fs::file_size(root/"original.raw"), "semantic filesystem provides original payload");
        auto events = ontology.journal(); require(!events.empty() && events[0].sessionKey == session.key, "ontology journal links work session");
        const auto before = events.size();
        ontology.apply({OntologyTypeAssignment{file, "Photo"}}, revision, session.key);
        require(ontology.revision() == revision && ontology.journal().size() == before, "idempotent classification adds no journal");
        store.erase(first.key, 2, session.key);
        require(ontology.listDirectory("Projects/First").files.empty(), "deleted files leave live semantic directories");
        query.includeDeleted = true; require(ontology.queryFiles(query).files.size() == 1, "historical graph retains tombstoned file");
        require(store.validate(first.key, 1), "ontology never rewrites RAW payload history");
        store.endSession(session.key);
        rejects([&] { ontology.apply({OntologyEntity{"after-close", "Closed", ""}}, revision, session.key); });
    }
    {
        ObjectStore reader(root/"package", "ontology-test", ObjectStore::Access::ReadOnly);
        OntologyFileSystem ontology(reader);
        require(ontology.revision() == 2 && ontology.classes().size() == 4, "read-only ontology reopen");
        require(ontology.statements(OntologyResource::file(firstKey)).size() == 2, "typed statements persist");
        rejects([&] { ontology.apply({OntologyEntity{"no", "Read only", ""}}, 2, sessionKey); });
        std::stop_source stop; stop.request_stop();
        rejects([&] { ontology.queryFiles({}, {}, 256, stop.get_token()); });
    }
    fs::remove_all(root);
    std::cout << "Ontology hierarchy, typed relations, semantic directories, journal and file provision verified\n";
    return 0;
} catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
