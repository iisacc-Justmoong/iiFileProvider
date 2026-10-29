#include <iiFileProviderCoreIO.h>
#include <iiFileProviderNative.h>
#include <iiFileProviderStorage.h>
#include <iiFileProviderPresentation.h>
#include <iiSDKCore/Runtime/Serialization.h>
#include <iiSDKCore/Core/Time/Time.h>
#include "serialization_record.h"
#include <array>
#include <iostream>
#include <stdexcept>

namespace provider = iiFileProvider;
namespace storage = iiFileProvider::core;
using namespace ii::io;
using namespace std::chrono_literals;
int checks = 0;
void check(bool valid, const char *message) { ++checks; if (!valid) throw std::runtime_error(message); }
#include "atomic_contract.inc"
int main() {
    struct Fixture final {
        std::filesystem::path previous = std::filesystem::current_path();
        std::filesystem::path root = previous / ("coreio-" + std::to_string(ii::core::Clock::now().time_since_epoch().count()));
        Fixture() { provider::native::createDirectories(root); std::filesystem::current_path(root); }
        ~Fixture() {
            provider::native::Presentation::cancelPending(root);
            std::filesystem::current_path(previous);
            std::error_code error; std::filesystem::remove_all(root, error);
        }
    } fixture;
    try {
        const auto path = fixture.root / "stream.bin";
        auto missing = storage::open_file(path);
        check(!missing && missing.error().code == ErrorCode::not_found, "missing file remains missing");
        auto invalid = storage::open_file(path, {Access::read, Creation::truncate});
        check(!invalid && invalid.error().code == ErrorCode::invalid_argument && !std::filesystem::exists(path), "invalid options never create");
        const std::array<std::byte, 5> bytes{std::byte{0}, std::byte{1}, std::byte{128}, std::byte{255}, std::byte{42}};
        auto opened = storage::open_file(path, {Access::read_write, Creation::create_new});
        check(bool(opened), "provider creates a native Core stream");
        auto duplicate = storage::open_file(path, {Access::write, Creation::create_new});
        check(!duplicate && duplicate.error().code == ErrorCode::already_exists, "exclusive creation preserves original");
        auto alias = *opened;
        check(opened->write(bytes) == bytes.size() && bool(opened->close()), "binary write and close");
        check(!alias.size() && alias.size().error().code == ErrorCode::closed, "all aliases share close");
        check(provider::native::Presentation::waitForIdle(10s), "writable close presentation completes");
        const auto details = provider::native::Presentation::load(path);
        check(details && details->details.size == bytes.size() && !details->preview.bytes.empty(), "details and preview persist after writable close");
        provider::native::Presentation::removeCompanion(path);
        {
            auto readonly = storage::open_file(path);
            check(bool(readonly) && !readonly->write(bytes) && bool(readonly->close()), "read-only access preserved");
        }
        check(provider::native::Presentation::waitForIdle(10s) && !std::filesystem::exists(provider::native::Presentation::companionPath(path)), "read-only close does not publish");
        const auto implicit = fixture.root / "last-owner.bin";
        {
            auto owner = storage::open_file(implicit, {Access::read_write, Creation::open_or_create});
            check(bool(owner) && owner->write(bytes) == bytes.size(), "open-or-create");
            { auto last = *owner; check(bool(last.flush()), "shared owner flush"); }
            check(!std::filesystem::exists(provider::native::Presentation::companionPath(implicit)), "intermediate alias removal does not publish");
        }
        check(provider::native::Presentation::waitForIdle(10s), "RAII close completes presentation");
        const auto last_owner = provider::native::Presentation::load(implicit);
        check(last_owner && last_owner->details.size == 5, "last shared owner publishes final bytes");
        auto truncated = storage::open_file(path, {Access::write, Creation::truncate});
        check(truncated && truncated->size() == 0 && bool(truncated->close()), "truncate existing native file");
        files(); // Assertions moved from Core with the atomic transaction.
        ii::runtime::DynamicSerializer serializer;
        check(bool(serializer.register_type<StoredSample>("app.sample", 1)), "register custom record codec");
        const StoredSample record{7, "capture", {1.0f, 0.5f}};
        const auto encoded = serializer.serialize(ii::runtime::DynamicValue{record});
        check(bool(encoded), "record encode");
        auto transaction = storage::AtomicFile::create(fixture.root / "record.cbor");
        check(transaction && transaction->write(*encoded) == encoded->size() && bool(transaction->commit()), "serialized atomic commit");
        auto input = storage::open_file(fixture.root / "record.cbor");
        check(bool(input), "serialized file opens");
        ii::serialization::Bytes read(encoded->size());
        const auto received = input->read(read);
        const auto decoded = serializer.deserialize(read);
        check(received && received->bytes == read.size() && decoded && std::any_cast<StoredSample>(*decoded) == record, "file to exact typed dynamic value");
        check(bool(input->close()), "serialized input closes");
        const auto failedTarget = fixture.root / "missing-parent" / "never.bin";
        auto failed = storage::AtomicFile::create(failedTarget);
        check(!failed && !std::filesystem::exists(failedTarget), "atomic failure has no direct-write fallback");
        std::cout << checks << " Core/file-provider integration checks passed\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
