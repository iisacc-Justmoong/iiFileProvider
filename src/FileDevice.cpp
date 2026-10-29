#include "iiFileProviderDevice.h"
#include "File.h"
#include "Presentation_p.h"
#include <iiFileProviderNativeDevice.h>
#include <QResource>
#include <vector>
#include <algorithm>
#include <system_error>
#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QJniEnvironment>
#include <QCoreApplication>
#include <QtCore/qcoreapplication_platform.h>
#include <unistd.h>
#endif

namespace iiFileProvider {
namespace fs=std::filesystem;
namespace {
fs::path localPath(const QString& name) { return fs::path(File::absolutePath(name).toStdU16String()); }
QString message(const std::exception& error) { return QString::fromUtf8(error.what()); }
native::OpenOptions options(QIODevice::OpenMode mode) {
    if(!(mode&QIODevice::ReadWrite)||(mode&QIODevice::NewOnly&&mode&QIODevice::ExistingOnly))throw std::runtime_error("Invalid file open mode");
    const bool writing=mode&QIODevice::WriteOnly;
    if(!writing&&(mode&(QIODevice::Append|QIODevice::Truncate|QIODevice::NewOnly)))throw std::runtime_error("Write flags require write access");
    return {.read=bool(mode&QIODevice::ReadOnly),.write=writing,.append=bool(mode&QIODevice::Append),
        .truncate=bool(mode&QIODevice::Truncate)||(writing&&!(mode&QIODevice::ReadOnly)&&!(mode&QIODevice::Append)),
        .exclusive=bool(mode&QIODevice::NewOnly),.existing=bool(mode&QIODevice::ExistingOnly)};
}
fs::perms nativePermissions(FileDevice::Permissions value) {
    unsigned mode=0;
    if(value&(FileDevice::ReadOwner|FileDevice::ReadUser))mode|=0400;
    if(value&(FileDevice::WriteOwner|FileDevice::WriteUser))mode|=0200;
    if(value&(FileDevice::ExeOwner|FileDevice::ExeUser))mode|=0100;
    if(value&FileDevice::ReadGroup)mode|=0040;if(value&FileDevice::WriteGroup)mode|=0020;if(value&FileDevice::ExeGroup)mode|=0010;
    if(value&FileDevice::ReadOther)mode|=0004;if(value&FileDevice::WriteOther)mode|=0002;if(value&FileDevice::ExeOther)mode|=0001;
    return fs::perms(mode);
}
FileDevice::Permissions qtPermissions(fs::perms permissions) {
    if(permissions==fs::perms::unknown)return {};
    const auto mode=unsigned(permissions);FileDevice::Permissions result;
    if(mode&0400)result|=FileDevice::ReadOwner|FileDevice::ReadUser;
    if(mode&0200)result|=FileDevice::WriteOwner|FileDevice::WriteUser;
    if(mode&0100)result|=FileDevice::ExeOwner|FileDevice::ExeUser;
    if(mode&0040)result|=FileDevice::ReadGroup;if(mode&0020)result|=FileDevice::WriteGroup;if(mode&0010)result|=FileDevice::ExeGroup;
    if(mode&0004)result|=FileDevice::ReadOther;if(mode&0002)result|=FileDevice::WriteOther;if(mode&0001)result|=FileDevice::ExeOther;
    return result;
}
}
class FileDevice::Impl {
public:
    native::Device owned{false};native::Device* active=&owned;
    QString name;FileError error=NoError;QByteArray resource;
    qint64 resourcePosition=0;bool resourceOpen=false,present=true;
};
FileDevice::FileDevice(QObject* parent):QIODevice(parent),m_impl(std::make_unique<Impl>()){}
FileDevice::FileDevice(const QString& name,QObject* parent):FileDevice(parent){m_impl->name=name;}
FileDevice::~FileDevice(){close();}
QString FileDevice::fileName()const{return m_impl->name;}
void FileDevice::setFileName(const QString& name){if(isOpen())close();m_impl->name=name;}
FileDevice::FileError FileDevice::error()const noexcept{return m_impl->error;}
void FileDevice::unsetError(){m_impl->error=NoError;setErrorString({});}
void FileDevice::fail(FileError code,const QString& text){m_impl->error=code;setErrorString(text);}
void FileDevice::setPresentationEnabled(bool value){m_impl->present=value;}
bool FileDevice::useNativeDevice(native::Device& device,OpenMode mode){
    m_impl->active=&device;m_impl->resourceOpen=false;unsetError();
    if(!QIODevice::open(mode))return false;
    if(!device.isSequential())QIODevice::seek(device.pos());return true;
}
void FileDevice::releaseNativeDevice(){QIODevice::close();m_impl->active=&m_impl->owned;}
bool FileDevice::open(OpenMode mode){
    if(isOpen()){fail(OpenError,"Device is already open");return false;}
    try{
        const auto flags=options(mode);m_impl->present=true;
        if(m_impl->name.startsWith(':')){
            if(mode&WriteOnly)throw std::runtime_error("Resources are read-only");
            QResource resource(m_impl->name);if(!resource.isValid())throw std::runtime_error("Resource does not exist");
            m_impl->resource=resource.uncompressedData();m_impl->resourcePosition=0;m_impl->resourceOpen=true;unsetError();return QIODevice::open(mode);
        }
        m_impl->owned.open(localPath(m_impl->name),flags);return useNativeDevice(m_impl->owned,mode);
    }catch(const std::exception& e){fail(OpenError,message(e));return false;}
}
bool FileDevice::open(int fd,OpenMode mode,FileHandleFlags flags){
    if(isOpen())return false;try{auto opt=options(mode);opt.truncate=false;m_impl->owned.attach(fd,flags&AutoCloseHandle,opt);m_impl->present=false;return useNativeDevice(m_impl->owned,mode);}catch(const std::exception&e){fail(OpenError,message(e));return false;}
}
bool FileDevice::open(FILE* stream,OpenMode mode,FileHandleFlags flags){
    if(isOpen())return false;try{auto opt=options(mode);opt.truncate=false;m_impl->owned.attach(stream,flags&AutoCloseHandle,opt);m_impl->present=false;return useNativeDevice(m_impl->owned,mode);}catch(const std::exception&e){fail(OpenError,message(e));return false;}
}
void FileDevice::close(){
    if(!isOpen())return;const bool complete=isWritable()&&error()==NoError&&flush();
    QIODevice::close();
    try{m_impl->active->close();if(complete&&m_impl->present&&!m_impl->resourceOpen&&!m_impl->name.isEmpty())detail::presentLater(localPath(m_impl->name));}
    catch(const std::exception&e){fail(WriteError,message(e));}
    m_impl->resource.clear();m_impl->resourceOpen=false;m_impl->active=&m_impl->owned;
}
bool FileDevice::isSequential()const{return m_impl->resourceOpen?false:m_impl->active->isSequential();}
int FileDevice::handle()const{return m_impl->resourceOpen?-1:m_impl->active->handle();}
qint64 FileDevice::pos()const{return QIODevice::pos();}
qint64 FileDevice::size()const{
    try{if(m_impl->resourceOpen)return m_impl->resource.size();if(isOpen())return m_impl->active->size();return qint64(fs::file_size(localPath(m_impl->name)));}
    catch(...){return 0;}
}
qint64 FileDevice::bytesAvailable()const{return std::max(QIODevice::bytesAvailable(),std::max<qint64>(0,size()-pos()));}
bool FileDevice::atEnd()const{return bytesAvailable()==0;}
bool FileDevice::seek(qint64 position){
    if(!isOpen()||position<0)return false;
    try{if(m_impl->resourceOpen)m_impl->resourcePosition=position;else if(!m_impl->active->seek(position))return false;return QIODevice::seek(position);}
    catch(const std::exception&e){fail(PositionError,message(e));return false;}
}
qint64 FileDevice::readData(char* bytes,qint64 count){
    try{if(m_impl->resourceOpen){const auto available=std::max<qint64>(0,m_impl->resource.size()-m_impl->resourcePosition);const auto amount=std::min(count,available);if(amount>0)std::copy_n(m_impl->resource.constData()+m_impl->resourcePosition,amount,bytes);m_impl->resourcePosition+=amount;return amount;}return m_impl->active->read(bytes,count);}
    catch(const std::exception&e){fail(ReadError,message(e));return -1;}
}
qint64 FileDevice::writeData(const char* bytes,qint64 count){try{return m_impl->active->write(bytes,count);}catch(const std::exception&e){fail(WriteError,message(e));return -1;}}
bool FileDevice::flush(){try{if(m_impl->resourceOpen)return true;m_impl->active->flush();return true;}catch(const std::exception&e){fail(WriteError,message(e));return false;}}
bool FileDevice::syncToDisk(){try{m_impl->active->flush(true);return true;}catch(const std::exception&e){fail(WriteError,message(e));return false;}}
bool FileDevice::exists()const{return exists(fileName());}
bool FileDevice::exists(const QString& path){if(path.startsWith(':'))return QResource(path).isValid();try{return fs::exists(localPath(path));}catch(...){return false;}}
bool FileDevice::remove(){close();try{const bool ok=native::File::remove(localPath(fileName()));if(!ok)fail(RemoveError,"File does not exist");return ok;}catch(const std::exception&e){fail(RemoveError,message(e));return false;}}
bool FileDevice::remove(const QString& path){FileDevice file(path);return file.remove();}
bool FileDevice::copy(const QString& from,const QString& to){
    try{const auto destination=localPath(to);if(fs::exists(fs::symlink_status(destination)))return false;
        FileDevice input(from);if(!input.open(ReadOnly))return false;native::AtomicSave output;output.open(destination);
        std::vector<char> bytes(1024*1024);for(;;){const auto count=input.read(bytes.data(),bytes.size());if(count<0)return false;if(!count)break;output.device().write(bytes.data(),count);}
        output.setPermissions(nativePermissions(input.permissions()));return output.commit(false).succeeded;
    }catch(...){return false;}
}
bool FileDevice::copy(const QString& to){if(isWritable()&&!flush())return false;const bool ok=copy(fileName(),to);if(!ok)fail(CopyError,"File copy failed or destination exists");return ok;}
bool FileDevice::rename(const QString& from,const QString& to){
    try{return native::renameFileExclusive(localPath(from),localPath(to)).succeeded;
    }catch(...){return false;}
}
bool FileDevice::rename(const QString& to){close();const bool ok=rename(fileName(),to);if(ok)m_impl->name=to;else fail(RenameError,"File rename failed or destination exists");return ok;}
bool FileDevice::resize(qint64 count){
    if(!isOpen())return resize(fileName(),count);
    const auto position=pos();try{if(!m_impl->active->resize(count))return false;return seek(std::min(position,count));}catch(const std::exception&e){fail(ResizeError,message(e));return false;}
}
bool FileDevice::resize(const QString& path,qint64 count){FileDevice file(path);return file.open(ReadWrite|ExistingOnly)&&file.resize(count);}
FileDevice::Permissions FileDevice::permissions()const{try{return isOpen()&&!m_impl->resourceOpen?qtPermissions(m_impl->active->permissions()):permissions(fileName());}catch(...){return {};}}
FileDevice::Permissions FileDevice::permissions(const QString& path){try{if(path.startsWith(':'))return ReadOwner|ReadUser|ReadGroup|ReadOther;return qtPermissions(fs::status(localPath(path)).permissions());}catch(...){return {};}}
bool FileDevice::setPermissions(Permissions value){try{if(!isOpen())return setPermissions(fileName(),value);return m_impl->active->setPermissions(nativePermissions(value));}catch(const std::exception&e){fail(PermissionsError,message(e));return false;}}
bool FileDevice::setPermissions(const QString& path,Permissions value){try{fs::permissions(localPath(path),nativePermissions(value));return true;}catch(...){return false;}}
uchar* FileDevice::map(qint64 offset,qint64 bytes,MemoryMapFlags flags){try{return reinterpret_cast<uchar*>(m_impl->active->map(offset,bytes,flags&MapPrivateOption));}catch(const std::exception&e){fail(ResourceError,message(e));return nullptr;}}
bool FileDevice::unmap(uchar* address){try{return m_impl->active->unmap(reinterpret_cast<char*>(address));}catch(const std::exception&e){fail(ResourceError,message(e));return false;}}
QByteArray FileDevice::encodeName(const QString& path){return path.toUtf8();}
QString FileDevice::decodeName(const QByteArray& path){return QString::fromUtf8(path);}
bool FileDevice::reserveTemporary(const QString& pattern){try{m_impl->owned.openTemporary(fs::path(pattern.toStdU16String()));m_impl->name=File::pathString(m_impl->owned.path());m_impl->present=false;return useNativeDevice(m_impl->owned,ReadWrite);}catch(const std::exception&e){fail(OpenError,message(e));return false;}}

class SaveFile::Impl {public:native::AtomicSave writer;bool fallback=false,direct=false,cancelled=false;};
SaveFile::SaveFile(QObject* parent):FileDevice(parent),m_save(std::make_unique<Impl>()){}
SaveFile::SaveFile(const QString& name,QObject* parent):FileDevice(name,parent),m_save(std::make_unique<Impl>()){}
SaveFile::~SaveFile(){close();}
void SaveFile::setDirectWriteFallback(bool value){m_save->fallback=value;}
bool SaveFile::directWriteFallback()const{return m_save->fallback;}
bool SaveFile::open(OpenMode mode){
    if(isOpen()){fail(OpenError,"Atomic output is already open");return false;}
    if(!(mode&WriteOnly)||(mode&(ReadOnly|Append|NewOnly|ExistingOnly))){fail(OpenError,"Atomic output requires WriteOnly without append/exclusive flags");return false;}
    m_save->direct=false;m_save->cancelled=false;
#ifdef Q_OS_ANDROID
    if(fileName().startsWith("content://")){
        if(!m_save->fallback){fail(OpenError,"Document URIs require explicit direct-write permission");return false;}
        QJniEnvironment env;QJniObject context=QNativeInterface::QAndroidApplication::context();auto uri=QJniObject::callStaticObjectMethod("android/net/Uri","parse","(Ljava/lang/String;)Landroid/net/Uri;",QJniObject::fromString(fileName()).object<jstring>());
        auto resolver=context.callObjectMethod("getContentResolver","()Landroid/content/ContentResolver;");auto descriptor=resolver.callObjectMethod("openFileDescriptor","(Landroid/net/Uri;Ljava/lang/String;)Landroid/os/ParcelFileDescriptor;",uri.object<jobject>(),QJniObject::fromString("wt").object<jstring>());
        if(env->ExceptionCheck()||!descriptor.isValid()){env->ExceptionClear();fail(OpenError,"Android document provider denied the URI");return false;}
        const int fd=descriptor.callMethod<jint>("detachFd","()I");if(env->ExceptionCheck()){env->ExceptionClear();fail(OpenError,"Android descriptor transfer failed");return false;}
        const bool opened=FileDevice::open(fd,mode,AutoCloseHandle);if(!opened&&fd>=0)::close(fd);
        m_save->direct=opened;return opened;
    }
#endif
    try{m_save->writer.open(localPath(fileName()));setPresentationEnabled(false);return useNativeDevice(m_save->writer.device(),mode);}
    catch(const std::exception&e){
        if(m_save->fallback){const bool opened=FileDevice::open(mode);m_save->direct=opened;return opened;}
        fail(OpenError,message(e));return false;
    }
}
void SaveFile::close(){if(m_save->direct)FileDevice::close();else{releaseNativeDevice();m_save->writer.cancel();}}
void SaveFile::cancelWriting(){m_save->cancelled=true;if(!m_save->direct){releaseNativeDevice();m_save->writer.cancel();}fail(AbortError,"Atomic output was cancelled");}
bool SaveFile::commit(){
    if(!isOpen()||m_save->cancelled||error()!=NoError){close();return false;}
    if(m_save->direct){const bool ok=syncToDisk();FileDevice::close();return ok&&error()==NoError;}
    releaseNativeDevice();const auto result=m_save->writer.commit();if(!result.succeeded)fail(WriteError,QString::fromStdString(result.message));return result.succeeded;
}
bool SaveFile::resize(qint64 size){if(!isOpen()||!FileDevice::resize(size)){fail(ResizeError,"Cannot resize a closed or invalid atomic output");return false;}return true;}
bool SaveFile::setPermissions(Permissions value){if(m_save->direct)return FileDevice::setPermissions(value);try{return m_save->writer.setPermissions(nativePermissions(value));}catch(const std::exception&e){fail(PermissionsError,message(e));return false;}}

TemporaryFile::TemporaryFile(QObject* parent):TemporaryFile(QDir::tempPath()+"/iisacc-XXXXXX",parent){}
TemporaryFile::TemporaryFile(const QString& pattern,QObject* parent):FileDevice(parent),m_pattern(pattern){}
TemporaryFile::~TemporaryFile(){close();if(m_autoRemove&&m_reserved){std::error_code ignored;fs::remove(fs::path(fileName().toStdU16String()),ignored);}}
bool TemporaryFile::open(){if(isOpen())return true;if(m_reserved){const bool ok=FileDevice::open(ReadWrite|ExistingOnly);setPresentationEnabled(false);return ok;}m_reserved=reserveTemporary(m_pattern);return m_reserved;}
bool TemporaryFile::autoRemove()const{return m_autoRemove;}
void TemporaryFile::setAutoRemove(bool value){m_autoRemove=value;}
QString TemporaryFile::fileTemplate()const{return m_pattern;}
void TemporaryFile::setFileTemplate(const QString& value){if(!m_reserved)m_pattern=value;}

bool LockFile::lock(){return QLockFile::lock();}
bool LockFile::tryLock(int timeout){return QLockFile::tryLock(timeout);}
bool Directory::mkpath(const QString& path)const{return QDir::mkpath(path);}
bool Directory::mkdir(const QString& path)const{return QDir::mkdir(path);}
TemporaryDirectory::TemporaryDirectory()=default;
TemporaryDirectory::TemporaryDirectory(const QString& pattern):QTemporaryDir(pattern){}
TemporaryDirectory::~TemporaryDirectory(){if(autoRemove())detail::cancelPresentation(fs::path(path().toStdU16String()));}
bool TemporaryDirectory::remove(){detail::cancelPresentation(fs::path(path().toStdU16String()));return QTemporaryDir::remove();}
} // namespace iiFileProvider
