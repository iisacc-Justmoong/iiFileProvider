# Windows Compatibility 패키지 검증

Windows 일괄 검증은 기본 Native·CoreIO·Qt 호환 패키지의 설치 뒤 별도
`Compatibility` 구성을 `build/compatibility/build/`에 빌드한다. 같은 SDK 설치
prefix에 `iiFileProvider::Compatibility`를 제공하여 네이티브 iiSharedCanvas가
설치된 공개 CMake target으로 의존하도록 한다. 새 Qt 의존성을 추가하지 않는다.

`iiFileProvider.Compatibility.Contracts`는 실제 임시 파일에 두 번 게시한 바이트와
`.iisacc-lock`의 정리를 검사한다. 네이티브 iiSharedCanvas의 실제 문서 파일 왕복도
이 설치 패키지를 사용한다. macOS Congregation에서 수행하는 Qt 양방향 잠금 oracle과
Windows의 현재 실행 범위는 구분한다.
