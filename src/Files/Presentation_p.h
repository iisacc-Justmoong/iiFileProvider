#pragma once
#include <filesystem>
#ifdef IIFILEPROVIDER_HAS_PRESENTATION
#include <iiFileProviderPresentation.h>
#endif
namespace iiFileProvider::detail {
inline void presentLater(const std::filesystem::path &path) noexcept {
#ifdef IIFILEPROVIDER_HAS_PRESENTATION
    native::Presentation::enqueue(path);
#else
    (void)path;
#endif
}
inline void cancelPresentation(const std::filesystem::path &path) noexcept {
#ifdef IIFILEPROVIDER_HAS_PRESENTATION
    try { native::Presentation::cancelPending(path); } catch (...) {}
#else
    (void)path;
#endif
}
inline void removePresentation(const std::filesystem::path &path) noexcept {
#ifdef IIFILEPROVIDER_HAS_PRESENTATION
    try { native::Presentation::removeCompanion(path); } catch (...) {}
#else
    (void)path;
#endif
}
}
