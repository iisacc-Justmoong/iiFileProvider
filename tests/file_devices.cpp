#include "File.h"
#include "iiFileProviderDevice.h"
#include <QCoreApplication>
#include <QImage>
#include <stdexcept>
static void check(bool value) { if (!value) throw std::runtime_error("Provider device contract failed"); }
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    iiFileProvider::TemporaryDirectory directory(QString::fromLocal8Bit(argv[1])+"/devices-XXXXXX");
    check(directory.isValid());
    const auto path=directory.filePath("문서.bin");
    iiFileProvider::FileDevice stream(path);
    check(stream.open(QIODevice::WriteOnly|QIODevice::NewOnly)); check(stream.write("first")==5); stream.close();
    check(!stream.open(QIODevice::WriteOnly|QIODevice::NewOnly));
    check(stream.open(QIODevice::WriteOnly|QIODevice::Append)); check(stream.write("-next")==5);stream.close();
    { iiFileProvider::SaveFile staged(path); check(staged.open(QIODevice::WriteOnly));check(staged.write("broken")==6); }
    check(iiFileProvider::File::read(path)=="first-next");
    { iiFileProvider::SaveFile staged(path);check(staged.open(QIODevice::WriteOnly));check(staged.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner));check(staged.write("complete")==8);check(staged.commit()); }
    check(iiFileProvider::File::read(path)=="complete");
    check(iiFileProvider::FileDevice::copy(path,directory.filePath("copy.bin")));
    check(!iiFileProvider::FileDevice::copy(path,directory.filePath("copy.bin")));
    QImage image(2,2,QImage::Format_RGB32);image.fill(Qt::red);
    const auto png=directory.filePath("generated.png");
    check(iiFileProvider::File::saveImage(image,png));check(QImage(png)==image);
    QImage invalid;check(!iiFileProvider::File::saveImage(invalid,png));check(QImage(png)==image);
}
