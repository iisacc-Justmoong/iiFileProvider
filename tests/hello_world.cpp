#include <iiFileProvider.h>

#include <QtCore/QtGlobal>

#include <cstring>
#include <iostream>

static_assert(__cplusplus > 202002L, "C++23 or newer is required");
static_assert(QT_VERSION == QT_VERSION_CHECK(6, 8, 3), "Qt 6.8.3 is required");

int main()
{
    if (std::strcmp(qVersion(), "6.8.3") != 0) {
        std::cerr << "Unexpected Qt runtime version: " << qVersion() << '\n';
        return 1;
    }
    const QString message = iiFileProvider::helloWorld();
    std::cout << message.toStdString() << '\n';
    return message == QStringLiteral("Hello world!") ? 0 : 1;
}
