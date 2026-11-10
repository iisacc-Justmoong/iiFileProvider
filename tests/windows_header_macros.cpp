#include <QByteArray>
#include <QIODevice>
#include <QString>
#include <QUrl>
#include <algorithm>
#include <filesystem>
#include <limits>
#include <windows.h>

#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

#include <File.h>

#if !defined(min) || !defined(max)
#error "iiFileProvider public headers must preserve the consuming application's macros"
#endif
