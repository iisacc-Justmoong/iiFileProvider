#include <cstdlib>
#include <iiFileProviderNative.h>
#include <iostream>
#include <new>
#include <string_view>

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
int main() {
    using iiFileProvider::native::Authorship;
    int failures = 0;
    std::size_t injected = 0, successful = 0;
    const auto check = [&](bool value, const char *message) {
        if (!value) {
            ++failures;
            std::cerr << message << '\n';
        }
    };
    for (std::ptrdiff_t allocation = 0; allocation < 128; ++allocation) {
        Authorship value;
        const auto before = value.dump();
        bool changed = false;
        fault::remaining = allocation;
        fault::fired = false;
        try {
            value.recordChange("2026-10-27T12:34:56.1+09:00");
            changed = true;
        } catch (const std::bad_alloc &) {
        }
        fault::remaining = -1;
        if (fault::fired)
            ++injected;
        if (changed) {
            ++successful;
            check(value.revision() == 1 &&
                      value.dump().find("2026-10-27T03:34:56.100Z") != std::string::npos,
                  "timestamp: canonical offset/fraction preserved");
        } else
            check(value.revision() == 0 && value.dump() == before,
                  "timestamp: allocation rejection preserves authorship");
    }
    check(injected >= 3 && successful > 0,
          "timestamp: allocation and successful positions exercised");
    for (std::string_view bad :
         {"2026-10-27T12:34:56", "2026-10-27T12:34:56.Z", "2026-10-27T12:34:56.1234Z",
          "2026-10-27T12:34:56Zextra", "2026-10-27 12:34:56Z", "2026-10-27T12:34:56+2400",
          "2026-10-27T12:34:56+24:00", "2026-02-29T12:34:56Z", "2026-10-27T24:00:00Z"}) {
        Authorship value;
        bool rejected = false;
        try {
            value.recordChange(std::string(bad));
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected && value.revision() == 0,
              "timestamp: invalid lexical/calendar input rejected without mutation");
    }
    Authorship leap;
    leap.recordChange("2024-02-29T00:00:00.123Z");
    check(leap.revision() == 1, "timestamp: valid leap day retained");
    std::cout << "injected_positions=" << injected << " successful_positions=" << successful
              << '\n';
    return failures ? 1 : 0;
}
