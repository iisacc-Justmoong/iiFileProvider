#include <iiFileProviderCompatibility.h>
#include <cstdlib>
#include <iostream>
#define CHECK(x)do{if(!(x)){std::cerr<<__LINE__<<": " #x "\n";std::exit(1);}}while(false)
int main(int argc,char** argv){CHECK(argc==2);using namespace iiFileProvider::native;
    TemporaryDirectory directory{std::filesystem::path(argv[1])};const auto path=directory.path()/"compatible.data";
    writeCompatible(path,"first");CHECK(File::read(path,32)=="first");writeCompatible(path,"second");CHECK(File::read(path,32)=="second");
    auto lock=path;lock+=".iisacc-lock";CHECK(!std::filesystem::exists(lock));
    std::cout<<"COMPATIBILITY_OK replacement=true lock_cleanup=true\n";
}
