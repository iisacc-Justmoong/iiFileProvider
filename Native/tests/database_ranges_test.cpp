#include <algorithm>
#include <array>
#include <cstdlib>
#include <iiFileProviderDatabase.h>
#include <iiFileProviderNative.h>
#include <iostream>
#include <limits>
#include <new>

namespace observe {
thread_local bool enabled = false;
thread_local std::size_t total = 0, largest = 0;
} // namespace observe
void *operator new(std::size_t size) {
    if (observe::enabled) {
        observe::total += size;
        observe::largest = std::max(observe::largest, size);
    }
    if (auto *p = std::malloc(size ? size : 1))
        return p;
    throw std::bad_alloc();
}
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }

using namespace iiFileProvider::native::sql;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << __LINE__ << ": " #x "\n";                                                 \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)
namespace {
std::string hex(const std::array<std::uint8_t, 32> &data) {
    constexpr char digits[] = "0123456789abcdef";
    std::string text;
    for (auto b : data) {
        text += digits[b >> 4];
        text += digits[b & 15];
    }
    return text;
}
Blob all(Database &db, std::int64_t id = 1) {
    auto query = db.prepare("SELECT data FROM records WHERE id=?");
    CHECK(query);
    CHECK(query->bind(1, id));
    CHECK(query->step() == Step::row);
    auto value = query->value(0);
    CHECK(value && std::holds_alternative<Blob>(*value));
    return std::get<Blob>(*value);
}
void small(const std::filesystem::path &parent) {
    static_assert(int(Code::invalid_input) == 0 && int(Code::closed) == 1 && int(Code::busy) == 2 &&
                  int(Code::constraint) == 3 && int(Code::read_only) == 4 &&
                  int(Code::limit_exceeded) == 5 && int(Code::failure) == 6);
    iiFileProvider::native::TemporaryDirectory directory(parent);
    CHECK(directory.isValid());
    const auto path = directory.path() / std::filesystem::path(u8"범위.sqlite");
    Options options;
    options.mode = OpenMode::create;
    auto db = Database::open(path, options);
    CHECK(db);
    CHECK(db->configure_durable());
    CHECK(db->execute("CREATE TABLE records(id INTEGER PRIMARY KEY,data BLOB NOT NULL)"));
    Blob original(65536 + 37);
    for (std::size_t i = 0; i < original.size(); ++i)
        original[i] = (i * 73 + 19) % 256;
    {
        auto insert = db->prepare("INSERT INTO records VALUES(1,?)");
        CHECK(insert && insert->bind(1, original) && insert->step() == Step::done);
    }
    CHECK(db->execute("INSERT INTO records VALUES(0,zeroblob(0))"));
    CHECK(db->execute("INSERT INTO records VALUES(-1,x'616263')"));
    auto part = db->read_blob_range("records", "data", 1, 4091, 17);
    CHECK(part && *part == Blob(original.begin() + 4091, original.begin() + 4108));
    CHECK(db->read_blob_range("records", "data", 1, original.size(), 0) == Blob{});
    CHECK(db->read_blob_range("records", "data", 0, 0, 0) == Blob{});
    auto beyond = db->read_blob_range("records", "data", 1, original.size() + 1, 0);
    CHECK(!beyond && beyond.error().code == Code::invalid_input);
    CHECK(!db->read_blob_range("records", "data", 1, std::numeric_limits<std::uint64_t>::max(), 1));
    CHECK(!db->read_blob_range("records", "data", 1, original.size() - 1, 2));
    CHECK(!db->read_blob_range("missing", "data", 1, 0, 1));
    CHECK(!db->read_blob_range("records", "missing", 1, 0, 1));
    CHECK(!db->read_blob_range("records; DROP TABLE records", "data", 1, 0, 1));
    CHECK(all(*db) == original);
    CHECK(!db->read_blob_range(std::string("records\0hidden", 14), "data", 1, 0, 1));
    const auto digest = db->blob_sha256("records", "data", 1, original.size());
    CHECK(digest &&
          hex(*digest) == "d6efbb42260d9ca4ffe2e6e251b5356e1054c81d7a0102a494d6a0ddfcde900d");
    const auto empty = db->blob_sha256("records", "data", 0, 0);
    CHECK(empty &&
          hex(*empty) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    const auto abc = db->blob_sha256("records", "data", -1, 3);
    CHECK(abc && hex(*abc) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    auto capped = db->blob_sha256("records", "data", 1, original.size() - 1);
    CHECK(!capped && capped.error().code == Code::limit_exceeded);
    const std::array<std::uint8_t, 13> first{201, 232, 7,   38,  69, 100, 131,
                                             162, 193, 224, 255, 30, 61};
    const std::array<std::uint8_t, 7> last{1, 3, 5, 7, 9, 11, 13};
    const auto beforeA = std::span(original).subspan(4093, first.size()),
               beforeB = std::span(original).subspan(32765, last.size()),
               same = std::span(original).subspan(777, 5);
    const std::array patches{BlobRangePatch{32765, beforeB, last},
                             BlobRangePatch{4093, beforeA, first}, BlobRangePatch{777, same, same}};
    CHECK(!db->compare_exchange_blob_ranges("records", "data", 1, patches));
    Blob expected = original;
    std::copy(first.begin(), first.end(), expected.begin() + 4093);
    std::copy(last.begin(), last.end(), expected.begin() + 32765);
    {
        auto tx = Transaction::begin(*db);
        CHECK(tx);
        auto result = db->compare_exchange_blob_ranges("records", "data", 1, patches);
        CHECK(result && result->bytes_compared == 25 && result->bytes_written == 20);
        CHECK(db->in_transaction());
        CHECK(all(*db) == expected);
        const auto updated = db->blob_sha256("records", "data", 1, expected.size());
        CHECK(updated &&
              hex(*updated) == "4876c15e95276a6d19d00566c0647a6138ff0aaa22e8804956c67e817786e927");
        CHECK(tx->rollback());
    }
    CHECK(all(*db) == original);
    {
        auto tx = Transaction::begin(*db);
        CHECK(tx);
        CHECK(db->compare_exchange_blob_ranges("records", "data", 1, patches));
        CHECK(tx->commit());
    }
    CHECK(all(*db) == expected);
    {
        auto tx = Transaction::begin(*db);
        CHECK(tx);
        const std::array noop{BlobRangePatch{4093, first, first},
                              BlobRangePatch{expected.size(), {}, {}},
                              BlobRangePatch{4094, {}, {}}};
        auto result = db->compare_exchange_blob_ranges("records", "data", 1, noop);
        CHECK(result && result->bytes_compared == 13 && result->bytes_written == 0);
        auto none = db->compare_exchange_blob_ranges("records", "data", 1, {});
        CHECK(none && none->bytes_written == 0);
        CHECK(tx->commit());
    }
    // A later mismatch or malformed range cannot leave the first write behind,
    // even if the caller deliberately commits its outer transaction after error.
    const std::array<std::uint8_t, 1> goodBefore{expected[5]}, replacement{0x33},
        wrongBefore{std::uint8_t(expected[9] ^ 0xff)};
    const BlobRangePatch valid{5, goodBefore, replacement};
    {
        auto tx = Transaction::begin(*db);
        CHECK(tx);
        const std::array conflict{valid, BlobRangePatch{9, wrongBefore, replacement}};
        auto result = db->compare_exchange_blob_ranges("records", "data", 1, conflict);
        CHECK(!result && result.error().code == Code::conflict);
        CHECK(tx->commit());
    }
    CHECK(all(*db) == expected);
    {
        auto tx = Transaction::begin(*db);
        CHECK(tx);
        const std::array invalid{valid, BlobRangePatch{expected.size(), goodBefore, replacement}};
        auto result = db->compare_exchange_blob_ranges("records", "data", 1, invalid);
        CHECK(!result && result.error().code == Code::invalid_input);
        CHECK(tx->commit());
    }
    CHECK(all(*db) == expected);
    {
        auto tx = Transaction::begin(*db);
        CHECK(tx);
        CHECK(!db->compare_exchange_blob_ranges("records", "data", 1, std::array{valid, valid}));
        CHECK(!db->compare_exchange_blob_ranges("records", "data", 1,
                                                std::array{BlobRangePatch{5, goodBefore, {}}}));
        CHECK(!db->compare_exchange_blob_ranges(
            "records", "data", 1,
            std::array{BlobRangePatch{std::numeric_limits<std::uint64_t>::max(), goodBefore,
                                      replacement}}));
        CHECK(tx->commit());
    }
    CHECK(all(*db) == expected);
    Options ro;
    ro.mode = OpenMode::read_only;
    auto reader = Database::open(path, ro);
    CHECK(reader);
    CHECK(reader->read_blob_range("records", "data", 1, 5, 1) == Blob{expected[5]});
    {
        auto tx = Transaction::begin(*reader, false);
        CHECK(tx);
        auto refused =
            reader->compare_exchange_blob_ranges("records", "data", 1, std::array{valid});
        CHECK(!refused && refused.error().code == Code::read_only);
        CHECK(tx->rollback());
    }
    Options little;
    little.maximum_value_bytes = 1024;
    auto bounded = Database::memory(little);
    CHECK(bounded);
    CHECK(bounded->execute("CREATE TABLE records(id INTEGER PRIMARY KEY,data BLOB)"));
    CHECK(bounded->execute("INSERT INTO records VALUES(1,zeroblob(512))"));
    auto tooMuch = bounded->read_blob_range("records", "data", 1, 0, 1025);
    CHECK(!tooMuch && tooMuch.error().code == Code::limit_exceeded);
    {
        auto tx = Transaction::begin(*bounded);
        CHECK(tx);
        std::vector<BlobRangePatch> metadata(1024 / sizeof(BlobRangePatch) + 1);
        auto refused = bounded->compare_exchange_blob_ranges("records", "data", 1, metadata);
        CHECK(!refused && refused.error().code == Code::limit_exceeded);
        CHECK(tx->rollback());
    }
    Database closed;
    CHECK(!closed.read_blob_range("records", "data", 1, 0, 0));
    CHECK(!closed.blob_sha256("records", "data", 1, 0));
    CHECK(!closed.compare_exchange_blob_ranges("records", "data", 1, {}));
    // SQLite interprets identifiers literally, including quotes and Unicode.
    CHECK(db->execute("CREATE TABLE \"별표\"\" table\" (\"x\"\" bytes\" BLOB)"));
    CHECK(db->execute("INSERT INTO \"별표\"\" table\" VALUES(x'010203')"));
    CHECK((db->read_blob_range("별표\" table", "x\" bytes", 1, 0, 3) == Blob{1, 2, 3}));
    // Internal savepoint names may be nested under an equal caller name.
    {
        auto tx = Transaction::begin(*db);
        CHECK(tx);
        CHECK(db->execute("SAVEPOINT ii_fp_blob_ranges"));
        CHECK(db->compare_exchange_blob_ranges("records", "data", 1, std::array{valid}));
        CHECK(db->execute("ROLLBACK TO ii_fp_blob_ranges"));
        CHECK(db->execute("RELEASE ii_fp_blob_ranges"));
        CHECK(tx->commit());
    }
    CHECK(all(*db) == expected);
    CHECK(db->execute("CREATE TABLE protected(id INTEGER PRIMARY KEY,data BLOB UNIQUE)"));
    CHECK(db->execute("INSERT INTO protected VALUES(1,x'010203')"));
    {
        auto tx = Transaction::begin(*db);
        CHECK(tx);
        CHECK(db->execute("INSERT INTO records VALUES(22,x'1122')"));
        const std::array<std::uint8_t, 3> was{1, 2, 3}, now{4, 5, 6};
        auto refused = db->compare_exchange_blob_ranges("protected", "data", 1,
                                                        std::array{BlobRangePatch{0, was, now}});
        CHECK(!refused && db->in_transaction());
        CHECK((db->read_blob_range("protected", "data", 1, 0, 3) == Blob{1, 2, 3}));
        CHECK(tx->commit());
        CHECK((all(*db, 22) == Blob{0x11, 0x22}));
    }
    CHECK(db->execute("CREATE TABLE text_cells(id INTEGER PRIMARY KEY,data)"));
    CHECK(db->execute("INSERT INTO text_cells VALUES(1,'hello'),(2,42),(3,NULL)"));
    CHECK((db->read_blob_range("text_cells", "data", 1, 1, 3) == Blob{'e', 'l', 'l'}));
    CHECK(!db->read_blob_range("text_cells", "data", 2, 0, 1));
    CHECK(!db->read_blob_range("text_cells", "data", 3, 0, 0));
    CHECK(!db->read_blob_range("records", "data", 999, 0, 1));
    // Deferred transactions are admitted too; SQLite owns any lock promotion.
    {
        auto tx = Transaction::begin(*db, false);
        CHECK(tx);
        const std::array<std::uint8_t, 1> h{'h'}, j{'j'};
        CHECK(db->compare_exchange_blob_ranges("text_cells", "data", 1,
                                               std::array{BlobRangePatch{0, h, j}}));
        CHECK(tx->commit());
    }
    CHECK((db->read_blob_range("text_cells", "data", 1, 0, 5) == Blob{'j', 'e', 'l', 'l', 'o'}));
    *db = Database{};
    auto reopened = Database::open(path);
    CHECK(reopened && all(*reopened) == expected);
    std::cout << "BLOB_RANGES_OK bounded_reads=true cas=true no_partial_batch=true rollback=true "
                 "hash=true readonly=true\n";
}
void large(const std::filesystem::path &parent) {
    iiFileProvider::native::TemporaryDirectory directory(parent);
    CHECK(directory.isValid());
    const auto path = directory.path() / "8k-argb.sqlite";
    Options options;
    options.mode = OpenMode::create;
    options.maximum_value_bytes = 512 * 1024 * 1024;
    auto db = Database::open(path, options);
    CHECK(db);
    CHECK(db->configure_durable());
    CHECK(db->execute("PRAGMA cache_size=-2048"));
    CHECK(db->execute("CREATE TABLE records(id INTEGER PRIMARY KEY,data BLOB NOT NULL)"));
    CHECK(db->execute("INSERT INTO records VALUES(1,zeroblob(268435456))"));
    observe::total = observe::largest = 0;
    observe::enabled = true;
    const auto before = db->blob_sha256("records", "data", 1, 268435456);
    observe::enabled = false;
    CHECK(before &&
          hex(*before) == "a6d72ac7690f53be6ae46ba88506bd97302a093f7108472bd9efc3cefda06484");
    const std::array<std::uint8_t, 8> zeros{}, a{2, 4, 6, 8, 10, 12, 14, 16};
    const std::array<std::uint8_t, 4> b{17, 19, 23, 29};
    const std::array<std::uint8_t, 3> c{31, 37, 41};
    const std::array patches{BlobRangePatch{123, zeros, a},
                             BlobRangePatch{134217735, std::span(zeros).first(4), b},
                             BlobRangePatch{268435453, std::span(zeros).first(3), c}};
    auto tx = Transaction::begin(*db);
    CHECK(tx);
    observe::enabled = true;
    const auto patched = db->compare_exchange_blob_ranges("records", "data", 1, patches);
    const auto after = db->blob_sha256("records", "data", 1, 268435456);
    const auto region = db->read_blob_range("records", "data", 1, 120, 16);
    observe::enabled = false;
    CHECK(patched && patched->bytes_compared == 15 && patched->bytes_written == 15);
    CHECK(after &&
          hex(*after) == "1e6efcadfe1c96238a1254d771341e3f14e78a92ed40fb96bb6d8b8ac227bb50");
    CHECK(region && std::equal(a.begin(), a.end(), region->begin() + 3));
    CHECK(observe::largest < 65536 && observe::total < 65536);
    CHECK(tx->rollback());
    CHECK(db->blob_sha256("records", "data", 1, 268435456) == *before);
    std::cout << "BLOB_RANGES_8K_OK blob_bytes=268435456 compared_bytes=15 written_bytes=15 "
                 "largest_cpp_allocation="
              << observe::largest << " total_cpp_allocation=" << observe::total
              << " whole_digest_checked=true rollback=true\n";
}
} // namespace
int main(int argc, char **argv) {
    CHECK(argc >= 2);
    if (argc > 2)
        large(std::filesystem::path(argv[1]));
    else
        small(std::filesystem::path(argv[1]));
}
