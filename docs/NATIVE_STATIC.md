# 임베디드 네이티브 제공자의 정적 연결

`IIFILEPROVIDER_NATIVE_STATIC=ON`은 Linux 초기화 프로세스와 같은 독립 네이티브 실행 파일에 실제 Native 제공자를 정적으로 연결한다. 기본 데스크톱 공유 라이브러리 구성은 유지한다. 공개 타깃은 `IIFILEPROVIDERNATIVE_STATIC_DEFINE`을 소비자에 전달하여 Windows에서도 DLL 가져오기 선언을 사용하지 않도록 한다.

교차 빌드는 C와 C++ 대상 컴파일러·sysroot를 모두 지정하며, 시스템 SQLite가 없는 대상은 `IIFILEPROVIDER_USE_BUNDLED_SQLITE=ON`으로 기존 고정 버전 소스를 빌드한다. 파일 시스템, 프리뷰, 메타데이터 구현을 다른 모듈에 복제하지 않는다.

정적 구성의 `iiFileProvider.Native.StaticContract`는 공개 타깃의 컴파일 정의 전달과 한국어 경로 생성·쓰기·읽기·정리를 검사한다. 기존 Native 테스트도 동일한 타깃으로 연결하므로 정적 구성에서 그대로 실행한다.
