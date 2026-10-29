#include <iiFileProviderNativeDevice.h>
#include <iiFileProviderNativeDirectory.h>
#include <iiFileProviderNative.h>
#include <iiFileProviderPresentation.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <cstdio>
#include <atomic>
#include <thread>
#ifndef _WIN32
#include <unistd.h>
#endif
using namespace iiFileProvider::native;
static void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
int main(int argc,char**argv)try{
 TemporaryDirectory root(argc>1?argv[1]:".");auto path=root.path()/u8"한글.bin";
 Device file;file.open(path,{.write=true,.exclusive=true});check(file.write("first",5)==5,"write");file.close();
 try{file.open(path,{.write=true,.exclusive=true});check(false,"exclusive create");}catch(const FileError&){}
 file.open(path,{.read=true,.write=true,.append=true});check(file.seek(0),"seek append");check(file.write("-tail",5)==5,"append");file.close();check(File::read(path)=="first-tail","append ignores seek");
 file.open(path,{.read=true,.write=true,.existing=true});check(file.size()==10,"size");check(file.seek(2),"seek");check(file.resize(4),"resize");check(file.pos()==2,"resize preserves position");check(file.seek(0),"rewind");char bytes[4];check(file.read(bytes,4)==4&&std::string(bytes,4)=="firs","read");
 auto* mapped=file.map(1,2,true);check(mapped,"map unaligned");mapped[0]='X';check(file.unmap(mapped),"unmap");file.close();check(File::read(path)=="firs","private mapping preserves file");
 {AtomicSave save;save.open(path);save.device().write("discard",7);}check(File::read(path)=="firs","destructor discard");
 {AtomicSave save;save.open(path);save.device().write("cancelled",9);save.cancel();check(!save.commit().succeeded,"cancel prevents commit");}check(File::read(path)=="firs","cancel preserves file");
 {AtomicSave save;save.open(path);save.device().write("published",9);check(save.commit().succeeded,"commit");check(!save.commit().succeeded,"single commit");}check(File::read(path)=="published","complete publish");
 {AtomicSave save;save.open(path);save.device().close();check(!save.commit().succeeded,"closed/failed output cannot publish");}check(File::read(path)=="published","failed commit preserves original");
 auto missing=root.path()/"missing"/"output.bin";try{AtomicSave save;save.open(missing);check(false,"missing parent");}catch(const FileError&){}check(!std::filesystem::exists(missing.parent_path()),"atomic save never creates parent");
 const auto borrowedPath=root.path()/"borrowed-stream.bin";
#ifdef _WIN32
 auto* borrowed=::_wfopen(borrowedPath.c_str(),L"w+b");
#else
 auto* borrowed=std::fopen(borrowedPath.c_str(),"w+b");
#endif
 check(borrowed,"borrow stream");{Device d;d.attach(borrowed,false,{.read=true,.write=true});d.write("ok",2);d.close();}check(std::fseek(borrowed,0,SEEK_SET)==0&&std::fgetc(borrowed)=='o',"borrowed stream remains owned by caller");std::fclose(borrowed);

 auto renamed=root.path()/"renamed.bin";check(renameFileExclusive(path,renamed).succeeded,"exclusive rename");
 File::write(path,"occupied");check(!renameFileExclusive(renamed,path).succeeded,"rename never replaces");check(File::read(path)=="occupied"&&File::read(renamed)=="published","rejected rename keeps both files");
 {AtomicSave save;save.open(path);save.device().write("overwritten",11);check(!save.commit(false).succeeded,"exclusive publication refuses existing destination");}check(File::read(path)=="occupied","rejected publication retains bytes");
 const auto stage=root.path()/"directory-stage", destination=root.path()/u8"한글-package";
 File::createDirectories(stage);File::create(stage/"data","complete");
 const auto occupied=root.path()/"occupied-directory";
 File::createDirectories(occupied);File::create(occupied/"keep","original");
 const auto collision=publishDirectoryExclusive(stage,occupied);
 check(!collision.succeeded&&collision.diagnosticSuffix=="already_exists"
       &&File::read(occupied/"keep")=="original"&&File::read(stage/"data")=="complete",
       "directory collision preserves both trees");
 check(!publishDirectoryExclusive(stage,path).succeeded&&File::read(path)=="occupied",
       "a file destination is never replaced by a directory");
 check(!publishDirectoryExclusive(path,destination).succeeded&&File::read(path)=="occupied",
       "a regular file cannot be published as a directory");
 const auto otherParent=root.path()/"other-parent";File::createDirectories(otherParent);
 check(!publishDirectoryExclusive(stage,otherParent/"package").succeeded
       &&File::read(stage/"data")=="complete"&&!std::filesystem::exists(otherParent/"package"),
       "directory publication requires a common real parent");
 std::error_code linkError;
 const auto link=root.path()/"directory-link";
 std::filesystem::create_directory_symlink(stage,link,linkError);
 if(!linkError) {
  check(!publishDirectoryExclusive(link,destination).succeeded,
        "a symlink cannot be used as the staged directory");
  check(!publishDirectoryExclusive(stage,link).succeeded&&std::filesystem::is_symlink(link),
        "a symlink destination is never replaced");
 }
 linkError.clear();const auto dangling=root.path()/"dangling-directory-link";
 std::filesystem::create_directory_symlink(root.path()/"absent",dangling,linkError);
 if(!linkError) check(!publishDirectoryExclusive(stage,dangling).succeeded
       &&std::filesystem::is_symlink(dangling),"a dangling destination link is never replaced");
 const auto publication=publishDirectoryExclusive(stage,destination);
 if(!publication.succeeded) {
  check(publication.diagnosticSuffix=="unsupported"&&File::read(stage/"data")=="complete"
        &&!std::filesystem::exists(destination),"unsupported exclusive rename fails without mutation");
 } else {
 check(!std::filesystem::exists(stage)&&File::read(destination/"data")=="complete",
       "exclusive directory publication moves the complete tree");
 const auto first=root.path()/"race-first",second=root.path()/"race-second",winner=root.path()/"race-winner";
 File::createDirectories(first);File::createDirectories(second);
 File::create(first/"data","first");File::create(second/"data","second");
 std::atomic<int> successes{0};
 std::thread a([&]{if(publishDirectoryExclusive(first,winner).succeeded)++successes;});
 std::thread b([&]{if(publishDirectoryExclusive(second,winner).succeeded)++successes;});
 a.join();b.join();
 check(successes==1,"exactly one concurrent directory publisher succeeds");
 const auto published=File::read(winner/"data");
 check((published=="first"&&File::read(second/"data")=="second")
       ||(published=="second"&&File::read(first/"data")=="first"),
       "concurrent publication preserves the winning and losing trees");
 std::filesystem::path releasedName;
 {
  TemporaryDirectory owned(root.path());releasedName=owned.path();
  File::create(owned.path()/"data","published");
  check(publishDirectoryExclusive(owned.path(),root.path()/"released-package").succeeded,
        "temporary directory can be published");
  owned.release();
  check(!owned.isValid(),"released temporary directory no longer owns its name");
  File::createDirectories(releasedName);File::create(releasedName/"keep","replacement");
 }
 check(File::read(releasedName/"keep")=="replacement",
       "cleanup does not remove a new directory created at the released name");
 }
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
 int pipe[2];check(::pipe(pipe)==0,"pipe");::close(pipe[0]);
 {Device broken(false);broken.attach(pipe[1],true,{.write=true});try{broken.write("x",1);check(false,"closed peer must fail");}catch(const FileError&){check(broken.failed(),"broken pipe poisons device");}}
#endif
 Presentation::cancelPending(root.path());std::cout<<"NATIVE_DEVICE_OK\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
