#include <fstream>
#include <iiFileProviderNative.h>
#include <iostream>
#include <iiFileProviderPresentation.h>
#include <iiFileProviderStorage.h>
#include <stdexcept>
#include <atomic>
#include <thread>
using namespace iiFileProvider::native;
void check(bool value) {
    static int checks=0;
    ++checks;
    if (!value)
        throw std::runtime_error("Native provider contract failed: "+std::to_string(checks));
}
int main(int argc, char **argv) try {
    check(sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    auto author = FileAuthor::fromIisaccAccount(
        {{"sub", "subject"}, {"email", "fixture@example.com"}, {"displayName", "작성자 📄"}},
        "https://iisacc.com", "2026-10-14T10:00:00.000Z");
    check(author.has_value());
    Authorship metadata;
    check(metadata.setAuthor(*author));
    auto revision = metadata.revision();
    check(!metadata.setAuthor(*author));
    check(metadata.revision() == revision);
    metadata.recordChange();
    auto reopened = Authorship::fromDump(metadata.dump());
    check(reopened.has_value() && !reopened->hasActiveAuthor() &&
          reopened->revision() == revision + 1);
    if (argc == 2) {
        auto old = File::read(argv[1]);
        auto parsed = Authorship::fromDump(old);
        check(parsed.has_value() && parsed->firstEditor().has_value());
        check(parsed->firstEditor()->metadata().account.sub == "legacy-profile");
        check(!parsed->hasActiveAuthor());
    }
    struct VisibleDirectory final {
        std::filesystem::path value=std::filesystem::current_path()/("native-crud-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        VisibleDirectory() { File::createDirectories(value); }
        ~VisibleDirectory() { Presentation::cancelPending(value); std::error_code error; std::filesystem::remove_all(value,error); }
        const std::filesystem::path& path() const { return value; }
    } directory;
    auto path = directory.path() / "file.bin";
    File::write(path, std::string("a\0b", 3));
    check(File::read(path) == std::string("a\0b", 3));
    File::write(path, "changed");
    auto input = File::openRead(path);
    check(input->seek(2) && input->read(5) == "anged");
    std::filesystem::permissions(path, std::filesystem::perms::owner_read |
                                           std::filesystem::perms::owner_write);
    check(input->seek(0));
    File::write(path, "replacement");
    check(input->read(7)=="changed");
    check(File::read(path) == "replacement");
    const auto renameSource=directory.path()/"rename-source.bin";
    { OutputFile output(renameSource,std::ios::binary|std::ios::out);output<<"renamed";output.close();check(bool(output)); }
    renameFile(renameSource,path);
    check(!std::filesystem::exists(renameSource) && File::read(path)=="renamed");
    auto missing = directory.path() / "uncommitted";
    {
        StagedFile staged(missing);
        File::write(staged.path(), "temporary");
    }
    check(!std::filesystem::exists(missing));
    check(!Authorship::fromDump("{\"schemaVersion\":99}").has_value());
    auto exclusive=directory.path()/"exclusive.mid";
    File::create(exclusive,std::string("a\0b",3));
    check(File::read(exclusive)==std::string("a\0b",3));
    bool rejected=false;
    try { File::create(exclusive,"lost"); } catch (const FileError &error) { rejected=error.code()==FileCode::AlreadyExists; }
    check(rejected && File::read(exclusive)==std::string("a\0b",3));
    File::update(exclusive,std::string("a\0b",3),"updated");
    rejected=false;
    try { File::update(exclusive,"old","lost"); } catch (const FileError &error) { rejected=error.code()==FileCode::Conflict; }
    check(rejected && File::read(exclusive)=="updated");
    check(Presentation::waitForIdle(std::chrono::seconds(10)));
    check(Presentation::load(exclusive).has_value());
    check(File::remove(exclusive) && !File::remove(exclusive));
    check(!std::filesystem::exists(Presentation::companionPath(exclusive)));
    rejected=false;
    try { (void)File::remove(directory.path()); } catch (const FileError &) { rejected=true; }
    check(rejected && std::filesystem::exists(directory.path()));
    rejected=false;
    try { File::create(std::filesystem::path(std::string("bad\0path",8)),"lost"); } catch (const FileError &) { rejected=true; }
    check(rejected);
    rejected=false;
    try { (void)File::read(std::filesystem::path(path.string()+std::string("\0ignored",8))); }
    catch (const FileError &error) { rejected=error.code()==FileCode::InvalidPath; }
    check(rejected);
    rejected=false;
    try { File::update(exclusive,"old","lost"); } catch (const FileError &error) { rejected=error.code()==FileCode::Conflict; }
    check(rejected && !std::filesystem::exists(exclusive));
    const auto race=directory.path()/"race.bin";
    std::atomic<int> won=0,existing=0,failed=0;
    auto compete=[&](std::string bytes) {
        try { File::create(race,bytes); ++won; }
        catch (const FileError &error) { if (error.code()==FileCode::AlreadyExists) ++existing; else ++failed; }
    };
    std::thread first(compete,std::string(4096,'a')),second(compete,std::string(4096,'b'));
    first.join(); second.join();
    const auto winner=File::read(race);
    check(won==1 && existing==1 && failed==0 && (winner==std::string(4096,'a') || winner==std::string(4096,'b')));
#ifndef _WIN32
    const auto alias=directory.path()/"alias.bin";
    std::filesystem::create_symlink(race,alias);
    rejected=false;
    try { File::create(alias,"lost"); } catch (const FileError &error) { rejected=error.code()==FileCode::AlreadyExists; }
    check(rejected && File::read(race)==winner && std::filesystem::is_symlink(alias));
    check(File::remove(alias) && File::read(race)==winner);
#endif
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
