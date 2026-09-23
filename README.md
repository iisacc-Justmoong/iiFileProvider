# iiFileProvider

C++20과 Qt 6.8.3 Core를 사용하는 버전 0.5.0의 동적 라이브러리이다. `File`이 형식에 독립적인 파일 CRUD를, `Database`가 SQLite 저장 트랜잭션을 담당한다. iisacc.com 계정 모델을 확장한 `FileAuthor`가 파일 작성자의 신원·상세 프로필·기여 정보·작성 디바이스를 기록하고, `Authorship`이 최초 편집자 한 명과 이후 편집 참여자 명단을 영구 메타데이터로 보관한다. `FileLink`는 이름·URL 쌍의 선택적인 파일 링크를 표현하며, `AuthenticationToken`은 별도의 런타임 인증 토큰을 보유한다. 메타데이터 값 객체는 파일 I/O를 직접 수행하지 않는다. 로그인 HTTP 요청과 JWT 서명 검증은 이 SDK의 역할에 포함되지 않는다.

공개 저장소는 [iisacc-Justmoong/iiFileProvider](https://github.com/iisacc-Justmoong/iiFileProvider)이다. 2026-09-07에 헤더·네임스페이스·CMake 패키지·공유 라이브러리·설치 경로의 SDK 식별자를 `iiFileProvider`로 통일했다. 소비자는 아래의 새 헤더와 CMake 타깃을 사용하고 기존 빌드 캐시를 다시 구성해야 한다.

## 공개 API

`<src/iiFileProvider.h>` 하나로 작성자 모델·파일 링크·인증 토큰을 사용할 수 있다. 공개 헤더는 `src/FileAuthor.h`, `src/AuthenticationToken.h`, `src/Authorship.h`, `src/FileLink.h`이다. `Authorship`은 최초 편집자·참여자 구분, 작성자별 최초·최근 기여 시각, 파일 링크와 변경 번호를 보관하며 변경 직후 JSON 덤프를 갱신한다. 클래스는 QObject가 아닌 C++ 값 객체이며 Qt Network나 계정 매니저의 수명에 의존하지 않는다.

```cpp
#include <iiFileProvider.h>

QString error;
auto author = iiFileProvider::FileAuthor::fromIisaccAccount(
    accountJson, QUrl("https://iisacc.com"), QDateTime::currentDateTimeUtc(), &error);
if (author) {
    auto metadata = author->metadata();
    metadata.details.organization = "Example Studio";
    metadata.attribution.roles = {"creator", "editor"};
    // 파일 작성 시각은 호스트가 알고 있는 실제 시각을 명시한다.
    metadata.attribution.createdAt = actualFileCreationTime;
    if (author->setMetadata(metadata, &error)) {
        const QJsonObject fileMetadata = author->toJson(); // 토큰·로그인 세션 제외
    }
}
```

`fromIisaccAccount()`는 account 객체를, `fromIisaccAppSession()`은 `{account, session, ...}` 앱 응답을 받는다. 후자는 앱 디바이스와 세션 시각도 검사한다. `fromJson()`은 버전이 명시된 파일 작성자 메타데이터를 읽는다. 모두 오류 시 `std::nullopt`와 값이 포함되지 않은 오류를 반환한다.

`AuthenticationToken::create()`에 토큰 종류·계정 subject·서비스 origin·발급/만료 시각·토큰 원문을 명시한다. 만들어진 토큰은 `setAuthenticationToken()`으로 작성자 객체에 연결한다. `secret()`만 원문을 돌려주며, `toJson()`과 디버그 출력에 원문을 넣지 않는다. `isWithinValidityWindow(at)`는 시간 범위 검사이며 인증 성공이나 접근 권한을 증명하지 않는다. iisacc.com 앱의 토큰은 JSON 응답이 아닌 HttpOnly 쿠키에 있으므로 호스트의 인증 계층이 별도로 관리해야 한다.

필드 목록, 관측한 서버 파일, 검증 조건과 예제는 [파일 작성자 계약](docs/FILE_AUTHOR_CONTRACT.md)에 있다.

### 최초 편집자와 편집 참여자

첫 번째로 성공한 `setAuthor()`가 최초 편집자 한 명을 고정한다. 이후 처음 등장한 계정은 참여자 목록 끝에 한 번만 추가된다. 계정 구분은 서비스 origin과 `sub` 조합이며, 표시 이름이나 이메일 변경은 새 참여자를 만들지 않는다. 최초 편집자는 참여자 목록에 중복 포함하지 않는다.

```cpp
iiFileProvider::Authorship history;
history.setAuthor(firstAuthor, firstEditTime); // 호스트가 제공한 FileAuthor와 실제 기록 시각
history.setAuthor(collaborator, nextEditTime);

const std::optional<iiFileProvider::FileAuthor> original = history.firstEditor();
const QList<iiFileProvider::FileAuthor> participants = history.participants();
history.clearActiveAuthor(); // 현재 편집 문맥만 해제하며 명단은 유지한다.

const QByteArray metadata = history.dump(); // 호스트가 파일 메타데이터에 저장한다.
auto restored = iiFileProvider::Authorship::fromDump(metadata);
```

조회 결과는 인증 토큰이 없는 복사본이다. 명단의 삭제·초기화·역할 교체 API는 없으며, 같은 계정의 프로필 갱신도 최초 편집자와 참여자 순서·최초 기여 시각을 유지한다. 최대 256명(최초 편집자 포함)·전체 메타데이터 2 MiB 한도에 도달하면 기존 기록을 지우지 않고 새 등록을 원자적으로 거절한다. 스키마 1·2의 기존 한도까지 채운 명단도 전체 기록을 보존하여 변환한다. 작성자 미지정 변경만 있는 파일은 최초 편집자가 없고 참여자는 빈 목록이다.

저장 스키마 3은 `firstEditor`에 최초 편집자 키 또는 null, `participants`에 이후 참여자 키 목록, `authors`에 각 키의 프로필·기여 시각, `links`에 선택적인 이름·URL 목록을 저장한다. 스키마 1·2 입력은 기존 명단과 revision을 보존하고 빈 링크 목록을 추가하여 읽는다. 소비자는 iiFileProvider 0.4.0 이상의 헤더·라이브러리로 다시 빌드해야 한다. 링크 저장 멤버 추가에 따라 공유 라이브러리 ABI 식별자도 `0.4`로 바뀌었다. 보존 계약은 같은 파일의 `Authorship`을 이어 사용하는 API와 저장·복원 경로에 적용되며, 외부 파일 변조나 별도의 빈 값으로 교체하는 행위를 막는 기능은 아니다.

### 이름과 URL 파일 메타데이터

`FileLink::fromString("[이름|URL]")` 또는 `FileLink::create(name, QString/QUrl)`로 링크를 만들고 작성자 등록 시 추가 인자로 넘긴다. HTTP(S) 전용 제한은 없으며 Society 주소, 로컬 파일, SMB·IPFS·URN·앱 스킴과 상대 URL을 받을 수 있다. 문자열로 받은 URL은 대소문자와 인코딩 원문을 보존하며 `urlText()`로 조회한다.

```cpp
auto address = iiFileProvider::FileLink::fromString("[Society 원본|society:document-1]");
if (address) {
    history.setAuthor(author, {*address}, actualEditTime);
}
// 작성자 등록과 별도로 파일 링크 목록 전체를 갱신할 수도 있다.
history.setLinksFromStrings({"[원본|file:///files/original.png]", "[참고|../reference.svg]"});
const auto fileLinks = history.links();
const auto metadata = history.dump();
```

추가 인자를 생략한 기존 `setAuthor(author, at)`는 링크를 유지한다. 명시적인 빈 링크 목록은 URL 메타데이터만 비우며 영구 편집자 명단을 유지한다. 링크는 등록된 URL을 자동으로 실행하거나 조회하지 않는다. 지원 형식·인코딩·원자적 갱신·버전 계약은 [파일 링크 문서](docs/FILE_LINKS.md)에 있다.

기존 소비자와의 호환을 위해 아래의 bootstrap API도 유지한다.

```cpp
#include <iiFileProvider.h>

const QString message = iiFileProvider::helloWorld();
```

`[[nodiscard]] QString iiFileProvider::helloWorld()`는 호출할 때마다 `Hello world!`를 반환한다. 공개 헤더와 구현은 소스 루트에 함께 배치한다. 외부 의존성은 기존 Qt 6.8.3 Core이며, 신규 외부 라이브러리를 도입하지 않았다. Qt의 사용 및 배포 조건은 설치된 Qt 라이선스에 따른다.

## 빌드, 테스트, 설치

CMake 3.24 이상, C++20 컴파일러 및 Qt 6.8.3이 필요하다. macOS에서는 `/Volumes/Storage/Qt/6.8.3/macos`가 존재하면 자동으로 탐색 경로에 추가한다.

```sh
./install.sh
```

단독 프로젝트로 구성할 때만 기본 설치 경로를 설정하므로 `add_subdirectory()`로 포함하는 상위 프로젝트의 설치 경로는 유지한다.

스크립트는 `build/`에서 Release 빌드 및 CTest를 실행하고, 기본 경로 `~/.local/SDK/iiFileProvider`에 설치한 뒤 `build/consumer/build/`에서 설치된 CMake 패키지만 사용하는 별도 실행 파일을 빌드하고 테스트한다. bootstrap 테스트는 반환 문자열, C++20 컴파일 설정, Qt 6.8.3 헤더 버전과 런타임 버전을 검사한다. 작성자 계약 테스트는 계정 매핑·상세 메타데이터 왕복·Unicode·잘못된 형식·세션 만료·토큰 바인딩·원문 제외·원자적 갱신을 소스 및 설치 소비자 양쪽에서 검사한다. `Authorship` 테스트는 최초 편집자 고정·참여자 순서·중복 방지·프로필 갱신·조회 복사본·파일 저장 후 복원·스키마 1·2 호환·잘못된 역할 참조·실패와 한도 초과 시 기록 보존도 양쪽에서 검사한다. `FileLink` 테스트는 다양한 스킴과 상대 주소·문자열/JSON/파일 왕복·인코딩·추가 인자의 원자적 기록·링크 생략과 비우기·명단 보존을 검증한다. 테스트 빌드에만 Qt Test를 사용한다.

설치 소비자 구성에는 현재 설치 경로의 패키지 디렉터리를 명시하므로 `INSTALL_PREFIX`를 변경해 재실행해도 이전 패키지 캐시를 사용하지 않는다.

설정은 명령행 인자 대신 환경 변수로 전달한다. `INSTALL_PREFIX`는 절대 경로여야 하며, `CMAKE_PREFIX_PATH`는 세미콜론 또는 콜론으로 구분한 추가 검색 경로를 받는다. 병렬 빌드 개수는 `CMAKE_BUILD_PARALLEL_LEVEL`로 지정하며 기본값은 2이다.

```sh
QT_PREFIX_PATH="/Volumes/Storage/Qt/6.8.3/macos" \
INSTALL_PREFIX="$HOME/.local/SDK/iiFileProvider" \
./install.sh
```

수동 실행 시에도 빌드 디렉터리는 `build/`를 사용한다.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DCMAKE_PREFIX_PATH="/Volumes/Storage/Qt/6.8.3/macos"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release
cmake -S tests/consumer -B build/consumer/build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$HOME/.local/SDK/iiFileProvider;/Volumes/Storage/Qt/6.8.3/macos"
cmake --build build/consumer/build --config Release
ctest --test-dir build/consumer/build -C Release --output-on-failure
```

## 설치 결과와 소비

기본 설치 경로에 `include/`의 umbrella·작성자·파일 링크·인증 토큰·export 헤더, `lib/`의 공유 라이브러리, `lib/cmake/iiFileProvider/`의 CMake 패키지, `share/iiFileProvider/`의 README와 계약 문서가 생성된다. 비공개 `src/JsonContract.h`는 설치하지 않는다. Windows 공유 라이브러리 실행 파일은 `bin/`에 설치된다. 소비자에게 C++20 및 `Qt6::Core` 링크 요구 사항을 전달한다. Qt를 묶어서 복사하지 않으며 설치된 Qt 런타임이 필요하다. 공유 라이브러리의 설치 RPATH는 링크에 사용한 외부 라이브러리 경로를 포함한다.

```cmake
find_package(iiFileProvider 0.5.0 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE iiFileProvider::iiFileProvider)
```

`CMAKE_PREFIX_PATH`에 SDK 설치 경로와 Qt 경로를 포함한다. 빌드·테스트·설치까지만 제공하며 커밋, 원격 업로드 또는 배포 단계는 없다.

## License

SPDX-License-Identifier: AGPL-3.0-only

iiFileProvider의 자체 작성 코드와 문서는 GNU Affero General Public License v3.0 전용으로
배포한다. 전체 조건은 [LICENSE](LICENSE)를 따른다.

Qt를 포함한 외부 라이브러리와 별도 고지가 있는 서드파티 코드는 각자의 라이선스를
유지한다. 이 프로젝트의 라이선스 선언은 해당 서드파티 라이선스를 대체하지 않는다.

## 계정 프로필 동기화

`fromIisaccAccount()`와 `fromIisaccAppSession()`는 iisacc.com의 `account.authorDetails`를 파일 작성자의
`metadata().details`에 반영한다. `toIisaccProfileUpdate()`는 웹 서비스가 허용하는 표시 이름과 작성자
프로필만 명시적으로 내보낸다. 인증 토큰과 파일별 귀속 정보는 계정 업데이트에 포함되지 않는다.

```cpp
const auto update = author->toIisaccProfileUpdate();
// The host authenticates and sends update as variables.input of the updateAccountAuthor GraphQL mutation.
```

계정 측 모델과 API는 서비스의 `docs/ACCOUNT_AUTHORS.md`, 파일 모델은
[FILE_AUTHOR_CONTRACT.md](docs/FILE_AUTHOR_CONTRACT.md)에 정의되어 있다.

## 파일 CRUD

0.5부터 파일 생성·읽기·갱신·삭제와 SQLite 저장 트랜잭션은 이 SDK가 소유한다. 다른 iisacc SDK를 참조하지 않으며 바이트, 스트림, 스키마를 입력으로 받는다. [전체 계약](docs/FILE_CRUD.md)을 따른다. 기존 0.4 값 타입의 ABI는 유지한다.

## Source layout

Implementation files and their headers live together under `src/`. Existing feature and platform subdirectories retain their responsibilities. Build configuration, tests, documentation, resources, and maintenance scripts remain at the project root. Configure and build using the repository-local `build/` directory.
