#include "File.h"
#include "iiFileProviderDevice.h"
#include <QCoreApplication>
#include <QImage>
#include <stdexcept>
#include <iostream>
#include <source_location>
#include <type_traits>
class QFile;
class QSaveFile;
static_assert(std::is_base_of_v<QIODevice, iiFileProvider::FileDevice>);
static_assert(!std::is_convertible_v<iiFileProvider::FileDevice*, QFile*>);
static_assert(!std::is_convertible_v<iiFileProvider::SaveFile*, QSaveFile*>);
static_assert(!std::is_convertible_v<iiFileProvider::TemporaryFile*, QFile*>);
static void check(bool value, const std::source_location where = std::source_location::current()) {
    if (!value) throw std::runtime_error("Provider device contract failed at line " + std::to_string(where.line()));
}
int main(int argc,char **argv) try {
    QCoreApplication app(argc,argv);
    iiFileProvider::TemporaryDirectory directory(QString::fromLocal8Bit(argv[1])+"/devices-XXXXXX");
    check(directory.isValid());
    const auto path=directory.filePath("문서.bin");
    iiFileProvider::FileDevice resource(":/iifp-device/registry.json");
    check(resource.open(QIODevice::ReadOnly));const auto resourceBytes=resource.readAll();check(!resourceBytes.isEmpty());
    check(resource.seek(0)&&resource.readAll()==resourceBytes);
    check(iiFileProvider::FileDevice::copy(resource.fileName(),directory.filePath("resource.json")));
    check(iiFileProvider::File::read(directory.filePath("resource.json"))==resourceBytes);
    { iiFileProvider::TemporaryFile temporary(directory.filePath("stage-XXXXXX.bin"));check(temporary.open());const auto stagedName=temporary.fileName();temporary.close();check(temporary.open());check(temporary.fileName()==stagedName); }

    iiFileProvider::FileDevice stream(path);
    check(stream.open(QIODevice::WriteOnly|QIODevice::NewOnly)); check(stream.write("first")==5); stream.close();
    check(!stream.open(QIODevice::WriteOnly|QIODevice::NewOnly));
    check(stream.open(QIODevice::WriteOnly|QIODevice::Append)); check(stream.write("-next")==5);stream.close();
    { iiFileProvider::SaveFile staged(path); check(staged.open(QIODevice::WriteOnly));check(staged.write("broken")==6); }
    check(iiFileProvider::File::read(path)=="first-next");
    { iiFileProvider::SaveFile staged(path);check(staged.open(QIODevice::WriteOnly));check(staged.setPermissions(iiFileProvider::FileDevice::ReadOwner|iiFileProvider::FileDevice::WriteOwner));check(staged.write("complete")==8);check(staged.commit()); }
    check(iiFileProvider::File::read(path)=="complete");
    {
        iiFileProvider::FileDevice input(path);
        check(input.open(QIODevice::ReadWrite|QIODevice::ExistingOnly));
        check(input.read(2)=="co"&&input.pos()==2);
        check(input.write("XY")==2&&input.pos()==4);
        check(input.seek(0)&&input.readAll()=="coXYlete");
        check(input.resize(4)&&input.size()==4&&input.pos()==4);
        check(input.seek(0));auto* map=input.map(1,2,iiFileProvider::FileDevice::MapPrivateOption);
        check(map);map[0]='z';check(input.unmap(map));input.close();
        check(iiFileProvider::File::read(path)=="coXY");
    }
    { iiFileProvider::SaveFile cancelled(path);check(cancelled.open(QIODevice::WriteOnly));check(cancelled.write("lost")==4);cancelled.cancelWriting();check(!cancelled.commit()); }
    check(iiFileProvider::File::read(path)=="coXY");
    { iiFileProvider::SaveFile closed(path);check(!closed.resize(0)); }
    check(iiFileProvider::File::read(path)=="coXY");
    { iiFileProvider::SaveFile moved(path);check(moved.open(QIODevice::WriteOnly));check(moved.seek(2));check(moved.write("!",1)==1);check(moved.commit()); }
    check(iiFileProvider::File::read(path)==QByteArray("\0\0!",3));
    check(iiFileProvider::FileDevice::copy(path,directory.filePath("copy.bin")));
    check(!iiFileProvider::FileDevice::copy(path,directory.filePath("copy.bin")));
    QImage image(2,2,QImage::Format_RGB32);image.fill(Qt::red);
    const auto png=directory.filePath("generated.png");
    check(iiFileProvider::File::saveImage(image,png));check(QImage(png)==image);
    QImage invalid;check(!iiFileProvider::File::saveImage(invalid,png));check(QImage(png)==image);
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
