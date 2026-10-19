# Windows 검증

Native 0.8.2 exposes `cache_change_token(path)` in the Python adapter and
`iifp_cache_change_token` in its C ABI. Identity and change time are read under
the same lock as preview metadata publication, avoiding intermediate NTFS change
times and mismatched Win32/Python volume identifiers. The adapter regression
preserves tokens across a preview and invalidates them after a restored-mtime edit.

Windows에서 Unicode 경로의 패키지 생성은 `_wsopen_s`의 `_O_EXCL`로 원자적으로 수행한다. 기존 패키지 덮어쓰기는 거부하며 `object_store` 런타임 테스트가 이를 검증한다.
# 소스 경로 검사

Windows에서는 파일과 모든 상위 경로의 reparse point를 네이티브 속성 조회로 검사한다. MinGW의 `canonical()` 결과만으로 디렉터리 심볼릭 링크를 판별하지 않는다. 패키징 테스트는 파일 링크와 상위 디렉터리 링크를 각각 생성하여 거부 동작을 검증한다.
# Directory publication

Preview sampling and NTFS alternate-stream publication share read, write and
delete access. A Windows regression holds DELETE access to a source while its
portable preview and embedded document are regenerated.

The Python `replace`/`rename` bridge uses native storage rename. On Windows it
drains presentation work for both paths, including children of a renamed
directory, before publication or rollback. The Python adapter regression moves
a directory with queued child previews and restores all of its original files.

