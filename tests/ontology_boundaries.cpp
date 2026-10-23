#include "OntologyFileSystem.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace iiFileProvider;
namespace fs=std::filesystem;
void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception &){rejected=true;}require(rejected,"invalid ontology input must reject");}
int main()try{
    const auto root=fs::path(OBJECT_TEST_DIRECTORY)/("ontology-bounds-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);{std::ofstream out(root/"source");out<<"bytes";}
    {
    ObjectStore store(root/"package","bounds",true);const auto s=store.beginSession({"local:test","editor","Editor"},"device","bounds");
    const auto file=store.importFile(root/"source","Files/source",s.key);OntologyFileSystem ontology(store);
    const auto a=OntologyResource::entity("a"),b=OntologyResource::entity("b"),c=OntologyResource::entity("c");
    ontology.apply({OntologyClass{"Asset","Asset","",{}},OntologyProperty{"link","Link",OntologyValueKind::Resource,"Asset","Asset"},
        OntologyProperty{"value","Value",OntologyValueKind::Real,"Asset"},OntologyProperty{"flag","Flag",OntologyValueKind::Boolean,"Asset"},
        OntologyEntity{"a","A",""},OntologyEntity{"b","B",""},OntologyEntity{"c","C",""},
        OntologyTypeAssignment{a,"Asset"},OntologyTypeAssignment{b,"Asset"},OntologyTypeAssignment{c,"Asset"},
        OntologyTypeAssignment{OntologyResource::file(file.key),"Asset"},
        OntologyAssertion{a,"link",OntologyValue::resource(b)},OntologyAssertion{b,"link",OntologyValue::resource(c)},OntologyAssertion{c,"link",OntologyValue::resource(a)},
        OntologyAssertion{OntologyResource::file(file.key),"value",OntologyValue::real(1.25)},
        OntologyAssertion{OntologyResource::file(file.key),"flag",OntologyValue::boolean(true)},
        OntologyAssertion{a,"value",OntologyValue::real(-0.0)}},0,s.key);
    const auto count=ontology.journal().size();
    ontology.apply({OntologyAssertion{a,"value",OntologyValue::real(0.0)}},1,s.key);
    require(ontology.revision()==1 && ontology.journal().size()==count,"positive and negative real zero are one semantic value");
    const auto walk=ontology.traverse(a,"link",OntologyDirection::Outgoing,32);
    require(walk.nodes.size()==2 && !walk.truncated && walk.nodes[0].resource==b && walk.nodes[1].resource==c,"bounded traversal deduplicates cyclic graph at shortest distance");
    require(ontology.traverse(a,"link",OntologyDirection::Outgoing,8,1).truncated,"traversal signals result limit");
    OntologyQuery query;query.conditions={{"value",OntologyValue::real(1.25)},{"flag",OntologyValue::boolean(true)}};
    require(ontology.queryFiles(query).files.size()==1,"real and boolean retain distinct typed values");
    rejects([&]{ontology.apply({OntologyAssertion{a,"value",OntologyValue::real(std::numeric_limits<double>::infinity())}},1,s.key);});
    rejects([&]{ontology.apply({OntologyEntity{"bad",std::string(1,'\xff'),""}},1,s.key);});
    rejects([&]{ontology.apply({OntologyEntity{"bad","",std::string("a\0b",3)}},1,s.key);});
    rejects([&]{ontology.apply({OntologyClass{"Bad","Bad","",{"Asset","Asset"}}},1,s.key);});
    rejects([&]{ontology.apply({OntologyClass{"Bad","Bad","",{"Missing"}}},1,s.key);});
    rejects([&]{ontology.apply({OntologyProperty{"bad","Bad",OntologyValueKind::Text,"Asset","Asset"}},1,s.key);});
    rejects([&]{ontology.apply({OntologyDirectory{"../escape","Escape",{}}},1,s.key);});
    rejects([&]{ontology.apply({OntologyTypeAssignment{OntologyResource::file("missing"),"Asset"}},1,s.key);});
    rejects([&]{OntologyQuery bad;bad.conditions={{"flag",OntologyValue::integer(1)}};ontology.queryFiles(bad);});
    rejects([&]{ontology.queryFiles({}, {},0);});
    rejects([&]{ontology.traverse(a,"link",OntologyDirection::Outgoing,33);});
    std::stop_source stop;stop.request_stop();
    rejects([&]{ontology.apply({OntologyEntity{"cancelled","Cancelled",""}},1,s.key,stop.get_token());});
    rejects([&]{ontology.traverse(a,"link",OntologyDirection::Outgoing,8,256,false,stop.get_token());});
    std::vector<OntologyChange> oversized(257,OntologyEntity{"big","Big",""});
    rejects([&]{ontology.apply(oversized,1,s.key);});
    oversized.clear();for(int i=0;i<256;++i)oversized.push_back(OntologyEntity{"large:"+std::to_string(i),std::string(4096,'x'),std::string(16384,'y')});
    rejects([&]{ontology.apply(oversized,1,s.key);});
    require(ontology.revision()==1 && !ontology.entity("large:0"),"batch journal budget rolls back all created nodes");
    // A definition update may not silently break a persisted virtual directory.
    ontology.apply({OntologyProperty{"unused","Unused",OntologyValueKind::Integer},OntologyDirectory{"Saved/Integer","Integers",{"",true,{{"unused",OntologyValue::integer(1)}}}}},1,s.key);
    rejects([&]{ontology.apply({OntologyProperty{"unused","Unused",OntologyValueKind::Text}},2,s.key);});
    require(ontology.property("unused")->kind==OntologyValueKind::Integer,"invalid saved-view schema update is atomic");
    ontology.apply({OntologyRemoveDirectory{"Saved/Integer"},OntologyProperty{"unused","Unused",OntologyValueKind::Text}},2,s.key);
    require(ontology.directories().empty(),"remove directory and update schema together");
    ontology.apply({OntologyAssertion{a,"link",OntologyValue::resource(b),false}},3,s.key);
    require(ontology.statements(a).size()==1 && ontology.statements(a)[0].propertyKey=="value","statement retraction preserves other facts");
    require(ontology.classes().size()==1 && ontology.ontologyClass("Asset").has_value(),"definition enumeration");
    require(ontology.revision()==4 && store.validate(file.key,1),"rejected inputs preserve file and ontology integrity");
    }
    fs::remove_all(root);std::cout<<"Ontology cancellation, bounds, cycles, typed values and saved-schema safety verified\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
