#pragma once
#include "iiFileProviderNativeExport.h"
#ifdef __cplusplus
extern "C" {
#endif
// UTF-8 paths. Errors are thread-local; callers never own the returned text.
IIFILEPROVIDER_NATIVE_EXPORT const char *iifp_last_error(void);
IIFILEPROVIDER_NATIVE_EXPORT int iifp_last_errno(void);
IIFILEPROVIDER_NATIVE_EXPORT int iifp_open(const char *path, int flags, int mode);
IIFILEPROVIDER_NATIVE_EXPORT int iifp_mkdir(const char *path, int recursive);
IIFILEPROVIDER_NATIVE_EXPORT int iifp_copy(const char *source, const char *destination, int overwrite);
IIFILEPROVIDER_NATIVE_EXPORT int iifp_publish(const char *source, const char *destination, int overwrite);
IIFILEPROVIDER_NATIVE_EXPORT void *iifp_stage(const char *destination);
IIFILEPROVIDER_NATIVE_EXPORT const char *iifp_stage_path(void *stage);
IIFILEPROVIDER_NATIVE_EXPORT int iifp_stage_publish(void *stage, int overwrite);
IIFILEPROVIDER_NATIVE_EXPORT void iifp_stage_discard(void *stage);
// Complete native file writes notify this owner. Derived presentation failure
// does not change the successful save result; callers can prepare synchronously.
IIFILEPROVIDER_NATIVE_EXPORT void iifp_present_later(const char *path);
IIFILEPROVIDER_NATIVE_EXPORT int iifp_present(const char *path);
// Thread-local UTF-8 JSON, including base64 preview; null means missing/stale.
IIFILEPROVIDER_NATIVE_EXPORT const char *iifp_presentation(const char *path);
IIFILEPROVIDER_NATIVE_EXPORT int iifp_presentation_wait(int timeout_ms);
IIFILEPROVIDER_NATIVE_EXPORT void iifp_presentation_cancel(const char *path);
#ifdef __cplusplus
}
#endif
