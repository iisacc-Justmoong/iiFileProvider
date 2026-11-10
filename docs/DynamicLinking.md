# 동적 라이브러리 빌드

파일 공급자의 Native, Temporary, Catalog, Objects, Compatibility와 선택적 Qt 어댑터는
기본적으로 공유 라이브러리로 빌드한다. `BUILD_SHARED_LIBS=OFF` 또는 Native의
`IIFILEPROVIDER_NATIVE_STATIC=ON`은 명시적인 정적 빌드다. 공개 CMake 타깃은
유지하며 Windows DLL, iOS 프레임워크와 WebAssembly 사이드 모듈을 지원한다.
앱은 사용하는 모듈 및 그 런타임 의존성을 자체 패키지에 포함한다.
별도로 구성하는 Core I/O 소비자 어댑터도 기본 공유 라이브러리로 제공한다.

테스트는 구성 요소의 종류와 실제 바이너리 연결, 저장 기능을 확인한다.
테스트 프로그램과 패키지 구조 검사는 앱 실행이나 기기 배포를 뜻하지 않는다.

Emscripten 소비자 설정은 공유 사이드 모듈을 가져오기 전에 CMake의 공유 라이브러리 지원을 활성화한다. 앱의 메인 모듈 컴파일·링크 옵션은 빌더 또는 앱이 지정해야 한다.
