#include <algorithm>
#include <array>
#include <cstdlib>
#include <iiFileProviderDatabase.h>
#include <iostream>
#include <new>
namespace fault {
thread_local std::ptrdiff_t remaining = -1;
thread_local bool fired = false;
} // namespace fault
void *operator new(std::size_t size) {
    if (fault::remaining == 0) {
        fault::remaining = -1;
        fault::fired = true;
        throw std::bad_alloc();
    }
    if (fault::remaining > 0)
        --fault::remaining;
    if (auto *p = std::malloc(size ? size : 1))
        return p;
    throw std::bad_alloc();
}
void *operator new[](std::size_t n) { return ::operator new(n); }
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
int main() {
    const std::string table = "records_" + std::string(128, 't'),
                      column = "value_" + std::string(128, 'c');
    std::size_t refused = 0, successful = 0;
    for (int position = 0; position < 24; ++position) {
        auto db = Database::memory();
        CHECK(db);
        CHECK(
            db->execute("CREATE TABLE " + table + " (id INTEGER PRIMARY KEY," + column + " BLOB)"));
        CHECK(db->execute("INSERT INTO " + table + " VALUES(1,zeroblob(512))"));
        auto tx = Transaction::begin(*db);
        CHECK(tx);
        const std::array<std::uint8_t, 4> before{}, after{1, 2, 3, 4};
        const std::array ranges{BlobRangePatch{7, before, after},
                                BlobRangePatch{400, before, after}};
        fault::remaining = position;
        fault::fired = false;
        auto changed = db->compare_exchange_blob_ranges(table, column, 1, ranges);
        fault::remaining = -1;
        if (!changed) {
            ++refused;
            CHECK(fault::fired && changed.error().code == Code::limit_exceeded);
        } else {
            ++successful;
            CHECK(changed->bytes_written == 8);
        }
        auto left = db->read_blob_range(table, column, 1, 7, 4),
             right = db->read_blob_range(table, column, 1, 400, 4);
        CHECK(left && right);
        const Blob expected =
            changed ? Blob(after.begin(), after.end()) : Blob(before.begin(), before.end());
        CHECK(*left == expected && *right == expected);
        CHECK(tx->commit());
    }
    CHECK(refused >= 3 && successful > 0);
    auto db = Database::memory();
    CHECK(db);
    CHECK(db->execute("CREATE TABLE " + table + " (id INTEGER PRIMARY KEY," + column + " BLOB)"));
    CHECK(db->execute("INSERT INTO " + table + " VALUES(1,x'616263')"));
    std::size_t readRefused = 0, hashRefused = 0;
    for (int position = 0; position < 8; ++position) {
        fault::remaining = position;
        fault::fired = false;
        auto r = db->read_blob_range(table, column, 1, 0, 3);
        fault::remaining = -1;
        if (!r) {
            ++readRefused;
            CHECK(fault::fired && r.error().code == Code::limit_exceeded);
        } else
            CHECK((*r == Blob{'a', 'b', 'c'}));
    }
    for (int position = 0; position < 8; ++position) {
        fault::remaining = position;
        fault::fired = false;
        auto r = db->blob_sha256(table, column, 1, 3);
        fault::remaining = -1;
        if (!r) {
            ++hashRefused;
            CHECK(fault::fired && r.error().code == Code::limit_exceeded);
        } else
            CHECK((*r)[0] == 0xba && (*r)[31] == 0xad);
    }
    CHECK(readRefused >= 2 && hashRefused >= 2);
    std::cout << "BLOB_RANGE_ALLOCATION_OK cas_refused=" << refused
              << " cas_successful=" << successful << " read_refused=" << readRefused
              << " hash_refused=" << hashRefused << " partial_batches=false\n";
}
