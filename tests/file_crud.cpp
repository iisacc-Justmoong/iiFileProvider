#include "iiFileProvider.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QTemporaryDir>
#include <cstdlib>
#include <iostream>

using namespace iiFileProvider;
void require(bool value, const char *message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
template<class F> void fails(FileCode code, F action) {
    try { action(); } catch (const FileError &error) {
        require(error.code() == code, "wrong failure classification"); return;
    }
    require(false, "operation unexpectedly succeeded");
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    require(directory.isValid(), "temporary directory");
    const auto path = directory.filePath(QString::fromUtf8("문서.unknown"));
    QString abandonedStage;
    {
        StagedFile staged(path);
        abandonedStage = staged.path();
        const auto moved = abandonedStage + ".moved";
        require(QFile::rename(abandonedStage, moved), "stage releases native file handle");
        require(QFile::rename(moved, abandonedStage), "restore stage for cleanup");
    }
    require(!QFile::exists(abandonedStage), "unpublished stage is removed");
    const QByteArray initial("\0payload\xff", 9);
    File::create(path, initial);
    require(File::read(path) == initial, "opaque bytes round trip");
    fails(FileCode::AlreadyExists, [&] { File::create(path, "replace"); });
    fails(FileCode::LimitExceeded, [&] { (void)File::read(path, 8); });
    require(File::readPrefix(path, 2) == initial.first(2), "prefix read");
    fails(FileCode::Conflict, [&] { File::update(path, "stale", "lost update"); });
    require(File::read(path) == initial, "rejected update preserves data");
    {
        QLockFile anotherWriter(path + ".iisacc-lock");
        require(anotherWriter.tryLock(0), "competing writer lock");
        fails(FileCode::Conflict, [&] { File::update(path, initial, "locked update"); });
        fails(FileCode::Conflict, [&] { File::remove(path); });
    }
    File::update(path, initial, "next");
    try {
        File::writeWith(path, [](QIODevice &output) {
            output.write("partial"); throw std::runtime_error("encoder failed");
        });
        require(false, "encoder exception");
    } catch (const std::runtime_error &) {}
    require(File::read(path) == "next", "failed encoder preserves original");
    File::writeWith(path, [](QIODevice &output) {
        require(output.write("complete") == 8, "stream write");
    });
    require(File::read(path) == "complete", "stream commit");
    {
        StagedFile staged(path);
        File::write(staged.path(), "validated codec output");
        require(staged.publish().succeeded, "publish provider-owned stage");
    }
    require(File::read(path) == "validated codec output", "stage reaches final path");
    File::write(path, "complete");
    require(!File::publish(std::filesystem::path(path.toStdU16String()),
                          std::filesystem::path(path.toStdU16String())).succeeded,
            "publishing a path over itself is rejected");
    const auto oldPermissions = QFile::permissions(path);
    File::write(path, "complete");
    require(QFile::permissions(path) == oldPermissions, "replacement preserves permissions");
    const auto copy = directory.filePath("copy.iisc");
    File::copy(path, copy);
    require(File::read(copy) == "complete", "copy bytes");
    require(File::remove(copy) && !File::remove(copy), "delete and missing delete");
    fails(FileCode::InvalidPath, [&] { File::create({}, {}); });
    fails(FileCode::InvalidPath, [&] { File::write(QString::fromUtf8("a\0b", 3), {}); });
    fails(FileCode::InvalidPath, [&] { File::remove(directory.path()); });
    require(File::remove(path), "delete document");
    const auto stageDirectory = directory.filePath("stage-directory");
    const auto finalDirectory = directory.filePath("final-directory");
    File::createDirectories(stageDirectory);
    File::publishDirectory(stageDirectory, finalDirectory);
    File::createDirectories(stageDirectory);
    fails(FileCode::AlreadyExists, [&] { File::publishDirectory(stageDirectory, finalDirectory); });
    fails(FileCode::NotFound, [&] { (void)File::read(path); });
    require(QDir(directory.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty(),
            "no abandoned temporary or lock files");
}
