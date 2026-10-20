#include "File.h"
#include <iiFileProviderPresentation.h>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QIODevice>
#include <future>
#include <mutex>
#include <chrono>
#include <iostream>
using namespace std::chrono_literals;
using iiFileProvider::native::SourceAccess;
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    if(argc!=2)return 2;
    QTemporaryDir directory(QString::fromUtf8(argv[1])+"/source-access-XXXXXX");
    if(!directory.isValid())return 2;
    const auto path=directory.filePath("source.bin");
    iiFileProvider::File::write(path,"source bytes");
    if(!iiFileProvider::native::Presentation::waitForIdle(5s))return 3;
    bool readBlocked=false,closeBlocked=false;
    std::future<QByteArray> read;
    {
        SourceAccess access(std::filesystem::path(path.toStdU16String()));std::lock_guard lock(access);
        std::promise<void> entered;auto started=entered.get_future();
        read=std::async(std::launch::async,[&]{entered.set_value();return iiFileProvider::File::read(path);});
        started.wait();readBlocked=read.wait_for(100ms)==std::future_status::timeout;
    }
    const bool content=read.get()==QByteArray("source bytes");
    std::promise<void> opened,closeNow;auto ready=opened.get_future();auto closeSignal=closeNow.get_future();
    auto closing=std::async(std::launch::async,[&]{auto input=iiFileProvider::File::openRead(path);opened.set_value();closeSignal.wait();input.reset();});
    ready.wait();
    {
        SourceAccess access(std::filesystem::path(path.toStdU16String()));std::lock_guard lock(access);
        closeNow.set_value();closeBlocked=closing.wait_for(100ms)==std::future_status::timeout;
    }
    closing.get();
    auto idle=iiFileProvider::File::openRead(path);
    iiFileProvider::native::Presentation::enqueue(std::filesystem::path(path.toStdU16String()));
    const bool idleDoesNotLock=iiFileProvider::native::Presentation::waitForIdle(2s);
    const bool prefix=iiFileProvider::File::readPrefix(path,6)==QByteArray("source");
    idle.reset();
    iiFileProvider::native::Presentation::cancelPending(std::filesystem::path(directory.path().toStdU16String()));
    if(!readBlocked||!closeBlocked||!content||!prefix||!idleDoesNotLock) {
        std::cerr<<"readBlocked="<<readBlocked<<" closeBlocked="<<closeBlocked<<" content="<<content<<" prefix="<<prefix<<" idle="<<idleDoesNotLock<<'\n';return 1;
    }
    return 0;
}
