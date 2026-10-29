#include <iiFileProviderCoreIO.h>
#include <iiFileProviderPresentation.h>
#include <iiSDKCore/Core/File/File.h>
#include <iiSDKCore/Core/Path/PathRoot.h>
#include <array>
#include <iostream>
int main() {
    const auto directory = std::filesystem::current_path();
    const auto path = directory / "installed-coreio.txt";
    try {
        auto transaction = iiFileProvider::core::AtomicFile::create(path);
        const std::string text = "installed native runtime/storage adapter";
        if (!transaction || transaction->write(std::as_bytes(std::span(text))) != text.size() || !transaction->commit()) return 1;
        auto file = ii::core::File::open(ii::core::PathRoot(directory), path.filename(), iiFileProvider::core::open_file);
        std::array<std::byte, 128> buffer{};
        if (!file) return 2;
        const auto read = file->channel().read(buffer);
        if (!read || read->bytes != text.size() || !file->channel().close()) return 3;
        if (!iiFileProvider::native::Presentation::waitForIdle(std::chrono::seconds(10))) return 4;
        const auto details = iiFileProvider::native::Presentation::load(path);
        if (!details || details->details.size != text.size()) return 5;
        iiFileProvider::native::Presentation::cancelPending(path);
        iiFileProvider::native::Presentation::removeCompanion(path);
        std::filesystem::remove(path);
        std::cout << "installed CoreIO consumer passed\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
