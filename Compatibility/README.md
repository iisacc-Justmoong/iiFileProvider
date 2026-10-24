# FileProvider compatibility

`iiFileProvider::Compatibility` is a source-composed, Qt-free C++23 bridge for
the existing `.iisacc-lock` writer protocol. `FileWriteLock` reserves the lock
path exclusively, retains a platform lock/handle for its lifetime and publishes
the existing PID/application/hostname metadata. Native and former Qt writers
therefore exclude one another. `writeCompatible` holds that guard while the
Native provider stages and atomically publishes complete replacement bytes.

Only a proven dead process on the same host is eligible for stale-lock recovery.
The bridge verifies the held file identity before removing it and never removes
an active, foreign-host, malformed or unidentifiable owner's guard. It does not
add a Qt library or header dependency and does not alter the installed provider.

Congregation composes this target from source, exercises native exclusion and
dead-owner recovery without Qt, and uses QLockFile only as an independent test
oracle for compatibility in both directions. Windows MSVC x64 also builds and
runs `iiFileProvider.Compatibility.Contracts`, including repeated atomic
replacement and lock cleanup. This Windows check is distinct from Congregation's
macOS bidirectional Qt oracle. See [Windows verification](../docs/WindowsCompatibility.md).
