#include "ObjectSource.h"
#include "ObjectHash.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/clonefile.h>
#endif
#endif

namespace iiFileProvider {
namespace {
namespace fs=std::filesystem;
[[noreturn]] void error(const char *message) {throw std::runtime_error(message);}
std::string utf8(const fs::path &p){const auto value=p.u8string();return {reinterpret_cast<const char *>(value.data()),value.size()};}
fs::path direct(const fs::path &p){
    const auto absolute=fs::absolute(p).lexically_normal();
#ifdef _WIN32
    // MinGW's canonical() does not resolve Windows directory reparse points.
    // Check every component, including ancestors of an ordinary source file.
    auto prefix=absolute.root_path();
    for(const auto &part:absolute.relative_path()){
        prefix/=part;
        const auto attributes=GetFileAttributesW(prefix.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES)error("unavailable object source");
        if(attributes&FILE_ATTRIBUTE_REPARSE_POINT)error("redirected object source");
    }
#else
    if(fs::canonical(absolute)!=absolute)error("redirected object source");
#endif
    return absolute;
}
#ifdef _WIN32
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
ObjectSourceIdentity handleIdentity(HANDLE handle,const fs::path &path){
    BY_HANDLE_FILE_INFORMATION file{};FILE_BASIC_INFO basic{};
    if(!GetFileInformationByHandle(handle,&file) || !GetFileInformationByHandleEx(handle,FileBasicInfo,&basic,sizeof(basic))
        || (file.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))error("invalid object source handle");
    const auto size=(std::uint64_t(file.nFileSizeHigh)<<32)|file.nFileSizeLow;
    detail::ObjectHash hash;hash.update(utf8(path));hash.update(":"+std::to_string(file.dwVolumeSerialNumber)+":"+std::to_string(file.nFileIndexHigh)+":"+std::to_string(file.nFileIndexLow));
    hash.update(":"+std::to_string(size)+":"+std::to_string(basic.CreationTime.QuadPart)+":"+std::to_string(basic.LastWriteTime.QuadPart)+":"+std::to_string(basic.ChangeTime.QuadPart));
    return {hash.finish(),size};
}
void openSource(Handle &handle,const fs::path &path){
    handle.value=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(handle.value==INVALID_HANDLE_VALUE)error("cannot acquire stable object source");
}
#else
struct Handle {
    int value=-1;
    ~Handle(){if(value>=0)::close(value);}
    void reset(int fd){if(value>=0)::close(value);value=fd;}
};
int openDirect(const fs::path &path,int flags){
    Handle parent;parent.value=::open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(parent.value<0)error("cannot open source root");
    if(path==path.root_path()){const auto result=parent.value;parent.value=-1;return result;}
    for(const auto &part:path.parent_path().relative_path()){
        const auto next=::openat(parent.value,part.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        if(next<0)error("redirected or unavailable source ancestor");parent.reset(next);
    }
    const auto result=::openat(parent.value,path.filename().c_str(),flags|O_NOFOLLOW|O_CLOEXEC);
    if(result<0)error("cannot open direct object source");return result;
}
ObjectSourceIdentity statIdentity(const struct stat &file,const fs::path &path){
    if(!S_ISREG(file.st_mode) || file.st_size<0)error("invalid object source handle");
    detail::ObjectHash hash;hash.update(utf8(path));
    hash.update(":"+std::to_string(file.st_dev)+":"+std::to_string(file.st_ino)+":"+std::to_string(file.st_size));
#ifdef __APPLE__
    const auto m=file.st_mtimespec,c=file.st_ctimespec,b=file.st_birthtimespec;
    hash.update(":"+std::to_string(b.tv_sec)+":"+std::to_string(b.tv_nsec));
#else
    const auto m=file.st_mtim,c=file.st_ctim;
#endif
    hash.update(":"+std::to_string(m.tv_sec)+":"+std::to_string(m.tv_nsec)+":"+std::to_string(c.tv_sec)+":"+std::to_string(c.tv_nsec));
    return {hash.finish(),static_cast<std::uint64_t>(file.st_size)};
}
ObjectSourceIdentity handleIdentity(int fd,const fs::path &path){
    struct stat file{};if(::fstat(fd,&file)!=0)error("cannot inspect object source handle");
    return statIdentity(file,path);
}
#endif
}
class ObjectSource::Impl {
public:
    fs::path directory,file;
    ObjectSourceIdentity original;
    ~Impl(){std::error_code ignored;if(!file.empty())fs::remove(file,ignored);if(!directory.empty())fs::remove(directory,ignored);}
};
ObjectSourceIdentity ObjectSource::inspect(const fs::path &input){
    const auto path=direct(input);Handle source;
#ifdef _WIN32
    openSource(source,path);
    return handleIdentity(source.value,path);
#else
    // Inventory needs metadata, not a readable payload handle. Opening every
    // preview can trigger provider hydration or serial disk waits on macOS.
    // Keep the no-follow ancestor boundary and let snapshot acquisition later
    // recheck this identity against an actual payload descriptor.
    source.value=openDirect(path.parent_path(),O_RDONLY|O_DIRECTORY);
    struct stat file{};
    if(::fstatat(source.value,path.filename().c_str(),&file,AT_SYMLINK_NOFOLLOW)!=0)
        error("cannot inspect object source metadata");
    return statIdentity(file,path);
#endif
}
ObjectSource::ObjectSource(const fs::path &input,const fs::path &staging,std::stop_token cancellation)
    :ObjectSource(input,staging,cancellation,std::nullopt){}
ObjectSource::ObjectSource(const fs::path &input,const fs::path &staging,std::stop_token cancellation,
    std::optional<ObjectSourceIdentity> expectedIdentity):m_impl(std::make_unique<Impl>()){
    if(cancellation.stop_requested())error("object snapshot cancelled");
    const auto sourcePath=direct(input),parent=direct(staging);
    if(!fs::is_directory(parent))error("invalid snapshot staging directory");
    Handle source;
#ifdef _WIN32
    openSource(source,sourcePath);
#else
    source.value=openDirect(sourcePath,O_RDONLY|O_NONBLOCK);
#endif
    m_impl->original=handleIdentity(source.value,sourcePath);
    if(expectedIdentity && m_impl->original!=*expectedIdentity)error("source changed before snapshot; rescan required");
    static std::atomic_uint64_t counter=0;
    for(unsigned attempt=0;attempt<64;++attempt){
        const auto name="snapshot-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++counter);
        const auto candidate=parent/name;
        if(fs::create_directory(candidate)){m_impl->directory=candidate;break;}
    }
    if(m_impl->directory.empty())error("cannot create unique snapshot directory");
    fs::permissions(m_impl->directory,fs::perms::owner_all,fs::perm_options::replace);
    m_impl->file=m_impl->directory/"payload";
#ifdef _WIN32
    if(!CopyFileW(sourcePath.c_str(),m_impl->file.c_str(),TRUE))error("cannot copy stable object source");
#else
    bool cloned=false;
#if defined(__APPLE__) && !defined(IIFILEPROVIDER_TEST_COPY_SNAPSHOTS)
    Handle directory;directory.value=openDirect(m_impl->directory,O_RDONLY|O_DIRECTORY);
    cloned=fclonefileat(source.value,directory.value,"payload",CLONE_NOOWNERCOPY)==0;
#endif
    if(!cloned){
        Handle output;output.value=::open(m_impl->file.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
        if(output.value<0)error("cannot create snapshot payload");
        // Heap storage is required on native worker threads whose stacks can be
        // smaller than a 1 MiB copy buffer. Small snapshots use only their size.
        std::vector<char> buffer(static_cast<std::size_t>(std::clamp<std::uint64_t>(m_impl->original.size,1,1024*1024)));
        std::uint64_t copied=0;
        for(;;){
            if(cancellation.stop_requested())error("object snapshot cancelled");
            const auto count=::read(source.value,buffer.data(),buffer.size());
            if(count<0){if(errno==EINTR)continue;error("cannot read snapshot source");}
            if(count==0)break;
            if(static_cast<std::uint64_t>(count)>m_impl->original.size-copied)error("source grew while capturing object snapshot");
            std::size_t offset=0;
            while(offset<static_cast<std::size_t>(count)){
                const auto written=::write(output.value,buffer.data()+offset,count-offset);
                if(written<0 && errno==EINTR)continue;
                if(written<=0)error("cannot write source snapshot");offset+=written;
            }
            copied+=static_cast<std::uint64_t>(count);
        }
        if(::fsync(output.value)!=0)error("cannot synchronize source snapshot");
    }
#endif
    fs::permissions(m_impl->file,fs::perms::owner_read|fs::perms::owner_write,fs::perm_options::replace);
    if(cancellation.stop_requested())error("object snapshot cancelled");
    if(handleIdentity(source.value,sourcePath)!=m_impl->original || inspect(sourcePath)!=m_impl->original || fs::file_size(m_impl->file)!=m_impl->original.size)
        error("source changed while capturing object snapshot");
}
ObjectSource::~ObjectSource()=default;
const fs::path &ObjectSource::path() const{return m_impl->file;}
const ObjectSourceIdentity &ObjectSource::identity() const{return m_impl->original;}
}
