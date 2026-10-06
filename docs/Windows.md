# Windows 검증

Windows에서 Unicode 경로의 패키지 생성은 `_wsopen_s`의 `_O_EXCL`로 원자적으로 수행한다. 기존 패키지 덮어쓰기는 거부하며 `object_store` 런타임 테스트가 이를 검증한다.
# 소스 경로 검사

Windows에서는 파일과 모든 상위 경로의 reparse point를 네이티브 속성 조회로 검사한다. MinGW의 `canonical()` 결과만으로 디렉터리 심볼릭 링크를 판별하지 않는다. 패키징 테스트는 파일 링크와 상위 디렉터리 링크를 각각 생성하여 거부 동작을 검증한다.
