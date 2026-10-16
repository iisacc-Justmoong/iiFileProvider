#include <iiFileProviderNative.h>
#include <iiFileProviderStorage.h>
#include <FileProviderC.h>
#include <stdexcept>
static void check(bool condition) { if (!condition) throw std::runtime_error("Installed native file owner failed"); }
int main(int argc,char **argv) {
    namespace fp=iiFileProvider::native;
    fp::TemporaryDirectory root(argv[1]);
    const auto file=root.path()/"native.bin";
    { fp::OutputFile stream(file,std::ios::binary);stream<<"first";stream.close();check(bool(stream)); }
    { fp::OutputFile stream(file,std::ios::binary|std::ios::app);stream<<"-append"; }
    check(fp::File::read(file)=="first-append");
    { fp::StagedFile stage(file);fp::File::write(stage.path(),"unfinished"); }
    check(fp::File::read(file)=="first-append");
    const auto output=root.path()/"image.png";
    void *stage=iifp_stage(output.string().c_str());check(stage);
    const auto path=std::filesystem::path(iifp_stage_path(stage));
    check(path.extension()==".png");
    fp::File::write(path,"complete");check(iifp_stage_publish(stage,0)==0);iifp_stage_discard(stage);
    check(fp::File::read(output)=="complete");
}
