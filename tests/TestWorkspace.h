#pragma once

#include <cstdlib>
#include <filesystem>

#ifdef QT_CORE_LIB
#include <QtCore/QString>
#endif

namespace provider_test {

inline std::filesystem::path root(const std::filesystem::path& fallback)
{
#ifdef _WIN32
    if (const auto* value = _wgetenv(L"IIFILEPROVIDER_TEST_DIRECTORY"); value && *value)
        return std::filesystem::path(value);
#else
    if (const auto* value = std::getenv("IIFILEPROVIDER_TEST_DIRECTORY"); value && *value)
        return std::filesystem::path(value);
#endif
    return fallback;
}

#ifdef QT_CORE_LIB
inline QString qtRoot(const QString& fallback)
{
    const auto value = qEnvironmentVariable("IIFILEPROVIDER_TEST_DIRECTORY");
    return value.isEmpty() ? fallback : value;
}
#endif
}
