#include "iiFileProviderPresentation.h"
#include <jni.h>
extern "C" JNIEXPORT void JNICALL
Java_com_iisacc_iifileprovider_FileStorage_nativePresentLater(JNIEnv *env, jclass, jstring value) {
    if (!value) return;
    // JNI's UTF-16 view preserves supplementary Unicode filesystem names.
    const auto *chars = env->GetStringChars(value, nullptr);
    if (!chars) return;
    const auto length = env->GetStringLength(value);
    std::u16string path(reinterpret_cast<const char16_t *>(chars), static_cast<std::size_t>(length));
    env->ReleaseStringChars(value, chars);
    iiFileProvider::native::Presentation::enqueue(std::filesystem::path(path));
}
