# 파일 작성자와 인증 토큰 계약

2026-09-07 iisacc.com의 계정 모델과 맞춘 로컬 구현 계약이다. 0.2.1부터 서버의 `account_profiles.author_details`와 `account.authorDetails`를 파일의 `details`에 대응시킨다. 운영 배포나 실제 운영 사용자 조회를 확인했다는 의미가 아니다.

## 관측한 사용자 모델

| 서비스 파일 | 확인한 필드와 조건 |
| --- | --- |
| `backend/providers/accounts/cognito.js`, `verifiedAccount()` | 서버가 JWT를 검증하고 `email_verified === true`일 때 정규화한 `email`, 안정적인 `sub`를 반환 |
| `src/lib/server/auth/snapshot.js`, `publicAccountSnapshot()` | 공개 account는 `sub`, `email`, `displayName`, `userId`, `societyCloudMembership`, `avatarUrl`, `authorDetails` 일곱 필드 |
| `backend/app/services/accounts/profile_service.rb`, `present()` | `account_profiles[verified_sub]`의 이름·User ID·아바타·멤버십을 조회. 비밀번호 설정·마케팅 동의·약관 시각은 내부 정보 |
| `backend/app/services/accounts/registration_service.rb` | User ID는 `@[a-z0-9_]{3,30}`이며 등록·동의 시각과 비밀번호 설정 여부를 별도로 보관 |
| `backend/app/services/accounts/society_cloud_membership.rb` | 멤버십 값은 정확히 `Free`, `Plus`, `Pro`, `Enterprise` |
| `backend/app/services/accounts/login_session_service.rb` | 세션 관리 ID, 앱 디바이스, 생성·최근 접속·만료 시각. registry 비밀값은 43자리 base64url, Redis에는 SHA-256만 저장 |
| `backend/app/services/auth/service.rb`, `issue_session`, `resolve_session` | Cognito ID/refresh 쿠키와 registry 쿠키를 함께 사용. registry 만료·해제는 유효한 Cognito 토큰만으로 복구하지 않음 |
| `POST /Account/GraphQL`, `appSession` mutation | 코드 검증·refresh 성공의 JSON은 `{account, session, limits}`이며 인증 원문은 JSON에 없음 |

파일의 작성자 귀속은 멤버십·제품 소유권·라이선스·로그인 허용과 별개이다. `sub`를 email이나 User ID로 대체하지 않는다. 서버가 이메일을 검증한다는 사실만으로 로컬에서 입력받은 JSON에 인증 성공 상태를 부여하지 않는다.

## 객체 구성

`FileAuthor`는 아래의 `AuthorMetadata`와 선택적인 `AuthorLoginSession`, `AuthenticationToken`을 가진다. `metadata()`는 const 참조를 반환하고, 수정은 복사본을 편집한 뒤 `setMetadata()`로 원자적으로 적용한다. 검증 실패 시 이전 데이터와 토큰을 유지한다. `sub`·서비스 origin·디바이스 중 하나가 바뀌면 기존 로그인 세션과 토큰을 해제한다.

| 객체/필드 | 의미와 검증 |
| --- | --- |
| `IisaccAccount.sub` | 필수 `[A-Za-z0-9_-]{1,128}`. 계정의 안정적인 연결키 |
| `email` | 필수. trim·NFKC·소문자 정규화, 전체 254자와 local part 64자 제한, 서버 이메일 형식 |
| `displayName` | 선택적, NFC·trim 후 Unicode code point 80개. 기본 빈 문자열 |
| `userId` | 선택적. 비어 있거나 정확히 `@[a-z0-9_]{3,30}` |
| `societyCloudMembership` | 네 값의 C++ enum. 필드 누락은 서버의 기본값 `Free`, 잘못된 명시 값은 거절 |
| `avatarUrl` | null/누락 또는 `/media/avatars/<64자리 소문자 SHA-256>.webp`. 서비스 origin으로 해석하여 절대 QUrl로 보관 |
| `AuthorDetails.fullName` | 선택적 전체 이름, 160자. 표시 이름에서 추론하지 않음 |
| `givenName`, `additionalName`, `familyName`, `pseudonym` | 각 80자. 호스트가 명시한 이름 구성/필명 |
| `biography` | 4,096자. LF 줄바꿈 허용 |
| `organization`, `department`, `team`, `jobTitle` | 소속·부서·팀·직책, 각 160자 |
| `organizationId` | 호스트가 명시한 소속 식별자, 128자 |
| `contactEmail`, `phoneNumber` | 작성자 연락용 이메일·전화 문자열. 계정 이메일과 독립적이며 연락 동의나 검증 상태를 뜻하지 않음 |
| `locale`, `timeZone` | locale 태그 형식과 Qt가 인식하는 시간대 ID. 빈 값 허용 |
| `countryCode`, `region`, `city` | 대문자 2자 국가 코드 형식·지역·도시. IP나 기기 정보에서 추정하지 않음 |
| `links[]` | 최대 32개 `{relation, label, url}`. 종류 40자·표시명 160자·FullyEncoded HTTPS URL 2,048자 |
| `identifiers[]` | 최대 32개 `{scheme, value}`. 종류 40자·값 256자. ORCID·ISNI 등 외부 ID를 명시하며 진위는 확인하지 않음 |
| `FileAttribution.roles[]` | 최대 16개, 각 40자, 중복 없는 역할 문자열. 기본 빈 배열이며 creator/editor/translator 등 실제 기여를 명시 |
| `credit`, `copyrightNotice` | 크레딧과 저작권 표시, 각 1,024자 |
| `licenseIdentifier`, `licenseUrl` | 라이선스 명칭 128자·HTTPS URL. 사용 권한이나 소유권을 검증하는 증서가 아님 |
| `documentId`, `projectId`, `workspaceId` | 호스트가 명시하는 파일·프로젝트·작업 공간 식별자, 각 128자 |
| `createdAt`, `modifiedAt` | 실제 파일 작성/수정에 기여한 시각. 선택적이며 둘 다 있으면 수정 시각이 작성 시각 이후여야 함 |
| `serviceOrigin`, `capturedAt` | 명시적인 HTTPS 서비스 origin과 해당 account snapshot을 확보한 시각. 필수 |
| `AuthorDevice` | 아래의 앱 보고 작성 환경. 선택적이며 하드웨어 인증을 뜻하지 않음 |

이름·연락처·소속 필드는 계정의 선택적 작성자 프로필이다. 기본은 빈 값이며 본인이 입력한 정보만 저장한다. 파일별 기여 정보는 계정 상세 프로필과 분리한다. 실명 검증, 이메일 인증 boolean, 관리자 여부, 자동 생성한 파일 작성 시각은 넣지 않는다. 표시 라벨은 displayName → User ID → email 순서이다.

앱 디바이스는 서버와 같은 `id`(lowercase SHA-256 64자리), `type`(`pc` 또는 `tablet`), `name`(80자), `platform`(40자), `osVersion`(80자), `appId`(128자), `appVersion`(40자)이다. 모두 필수이며 OS만으로 form factor를 추론하지 않는다. 디바이스 ID는 호스트가 이미 해시한 식별자만 받으며 실제 머신 ID를 자동 조회하지 않는다.

## 명시적 입력 어댑터

- `fromIisaccAccount(account, serviceOrigin, capturedAt)`는 공개 account 객체를 읽는다. `sub`·email이 없는 부분 프로필은 거절하고, 내부 필드와 알 수 없는 추가 필드는 복사하지 않는다. `account.authorDetails`를 `metadata().details`로 검증해 읽는다. 오래된 account에서 누락된 선택 필드는 빈 값/Free/null로 구성한다. 명시적인 null/잘못된 authorDetails는 거절한다.
- `toIisaccProfileUpdate()`는 `{displayName, authorDetails}`를 반환한다. 서버 `POST /Account/GraphQL`의 `updateAccountAuthor(input: $input)` mutation에 `variables.input`으로 보낼 명시적 payload이며, 신원·멤버십·파일 귀속·디바이스·세션·인증 토큰을 내보내지 않는다. 실제 HTTP 전송과 인증은 호스트가 담당한다.
- `fromIisaccAppSession(response, serviceOrigin, capturedAt)`는 account에 더해 `session.id`, `client: app`, `current: true`, 모든 device 필드와 세 시각을 요구한다. `createdAt <= lastSeenAt <= capturedAt < expiresAt`를 검사하고 디바이스를 작성 환경으로 복사한다. 세션 생성 시각을 파일 작성 시각으로 사용하지 않는다.
- `fromJson(metadata)`는 아래의 schemaVersion 1 파일 메타데이터만 읽는다. 이때 토큰과 로그인 세션은 항상 비어 있다.

모든 입력에서 필드의 JSON 타입을 먼저 검사한다. 숫자를 문자열로 바꾸거나 null을 빈 이름으로 바꾸지 않는다. 문자열은 NFC·trim 후 code point 개수를 제한하며 제어 문자·줄/문단 구분자를 거절한다. biography의 LF만 허용한다. 시간 문자열에는 `Z` 또는 `±HH:MM`이 필요하며 UTC ISO 8601 밀리초로 저장한다. authorDetails/details는 64 KiB, account 입력은 128 KiB, 앱 응답은 192 KiB, 파일 메타데이터는 128 KiB 이하이다. 크기는 compact JSON의 UTF-8 바이트 기준이다.

서비스 origin에는 사용자정보·경로·query·fragment를 허용하지 않으며 마지막 `/`와 기본 HTTPS 포트를 정규화한다. 계정 아바타는 정확한 서버 경로만 허용한다. 상세 프로필의 외부 링크·라이선스 URL에는 HTTPS만 허용한다. 이 SDK는 URL에 접속하거나 이미지를 다운로드하지 않는다.

## 저장 형식과 토큰 경계

```json
{
  "schemaVersion": 1,
  "serviceOrigin": "https://iisacc.com",
  "capturedAt": "2026-09-07T10:00:00.000Z",
  "account": {
    "sub": "example-subject",
    "email": "author@example.com",
    "displayName": "Example Author",
    "userId": "@example_author",
    "societyCloudMembership": "Free",
    "avatarUrl": null
  },
  "details": { "organization": "Example Studio" },
  "attribution": { "roles": ["creator"], "createdAt": null, "modifiedAt": null },
  "device": null
}
```

읽기에서 누락된 details/attribution의 선택 필드는 기본값으로 채우고, 쓰기에서는 정의된 모든 필드를 명시한다. schemaVersion 누락·문자열·소수·다른 버전과 알 수 없는 저장 필드는 거절한다. 공용 API 응답 어댑터의 추가 필드 무시와 파일 저장 스키마의 엄격한 검사는 의도적으로 다르다.

`AuthenticationTokenInfo`는 종류(`IisaccSession`, `CognitoId`, `CognitoAccess`, `CognitoRefresh`), `subject`, `serviceOrigin`, 선택적 issuer·audience·scopes, 세션 관리 ID와 issuedAt/notBefore/expiresAt을 담는다. issuedAt·expiresAt은 필수이고 종료 시각은 시작 시각보다 커야 한다. notBefore가 있으면 발급 시각 이상·만료 시각 미만이다. 범위 검사는 만료 시각 자체를 제외한다.

원문은 별도의 private QByteArray에 보유한다. 1~16,384바이트의 공백 없는 출력 가능한 ASCII를 받고 registry 종류는 정확히 43자리 base64url과 32자리 hex 세션 관리 ID를 요구한다. 토큰 내부 JWT claim을 해독해 subject·issuer·audience·시각을 추론하지 않는다. 호스트 인증 계층이 검증한 메타데이터를 명시적으로 전달해야 한다. 서명 검증·갱신·취소·서버 접근 허용은 호스트 인증 계층의 책임이다.

```cpp
iiFileProvider::AuthenticationTokenInfo info;
info.kind = iiFileProvider::AuthenticationTokenKind::IisaccSession;
info.subject = author->metadata().account.sub;
info.serviceOrigin = author->metadata().serviceOrigin;
info.sessionId = author->loginSession()->id;
info.issuedAt = author->loginSession()->createdAt;
info.expiresAt = author->loginSession()->expiresAt;
auto token = iiFileProvider::AuthenticationToken::create(info, registryCookieValue, &error);
if (token && author->setAuthenticationToken(*token, &error)) {
    // 원문이 필요한 인증 전송 계층에서만 명시적으로 secret()에 접근한다.
}
```

이 예제는 유효한 앱 응답으로 만든 author와 호스트가 관리하는 쿠키 값이 있다는 전제이다. 서비스 앱 API는 이 쿠키를 Bearer 헤더로 받는 계약이 아니며 SDK가 임의로 요청 헤더를 생성하지 않는다. 공개 세션 ID는 로그인 비밀값을 대신할 수 없다. 멤버십은 인증 토큰의 scope로 자동 변환하지 않는다.

`FileAuthor::toJson()`에는 토큰 종류·원문·로그인 세션 관리 ID 자체가 들어가지 않는다. `AuthenticationToken::toRedactedJson()`은 별도 진단용 메타데이터와 `[REDACTED]`만 반환하고 QDebug도 원문을 출력하지 않는다. `secret()` 호출 및 값 객체의 명시적 복사는 호스트가 관리해야 한다. clear/destruction이 모든 Qt implicit-sharing 복사본의 메모리를 암호학적으로 소거한다고 보장하지 않는다.

파일 메타데이터에는 호스트가 설정한 이메일·연락처·디바이스가 포함된다. 파일 공개 범위에 맞는 작성자 정보 선택은 호스트가 한다. 비밀번호·OTP·refresh/ID/registry 토큰·마케팅 동의·약관 기록·token hash를 서버 account에서 자동 복사하지 않는다.

## 의존성과 검증

Qt Core의 JSON, Unicode, QUrl, QDateTime, QTimeZone을 재사용한다. 기존 Qt 6.8.3 외의 런타임 의존성은 추가하지 않는다. 이미 있는 iiAcountManager는 로그인·Qt Network·QObject 수명을 소유하고 현재 Account는 네 표시 필드만 노출하므로, 파일 작성자 값 객체가 그 매니저를 소유하거나 상위 앱에 의존하도록 만들지 않았다. 두 SDK는 명시적인 서버 JSON 계약으로 연결할 수 있다. 형식·문자열·시각의 API 동작은 [QJsonValue](https://doc.qt.io/qt-6.8/qjsonvalue.html), [QString](https://doc.qt.io/qt-6.8/qstring.html), [QDateTime](https://doc.qt.io/qt-6.8/qdatetime.html)의 공식 문서를 참고했다.

`tests/author_contract.cpp`는 실제 서버 필드 형태를 본뜬 합성 fixture로 account/앱 응답, rich metadata 왕복, 이전 모델 호환, 잘못된 필드, Unicode code point 경계, 내부 정보 제외, 세션 만료, 토큰 바인딩·시간 범위·redaction과 실패의 원자성을 검증한다. 같은 계약을 별도 설치 소비자에서 다시 컴파일·실행하여 공개 헤더와 공유 라이브러리 export를 확인한다. 실제 계정·운영 토큰·과금 요청은 사용하지 않는다.

2026-09-07 macOS arm64 / AppleClang 21 / Qt 6.8.3의 Release 빌드와 설치를 완료했다. 소스 CTest 2/2, 별도 설치 소비자 CTest 2/2, 기존 Society의 재빌드 및 CTest 2/2가 통과했다. 작성자 Qt Test 출력은 소스·설치본 각각 39 passed(초기화/정리 포함)이다. 설치된 공개 헤더·문서의 소스 일치, 공개 심볼 export, 런타임의 Qt Test/Network 의존성 부재를 확인했다. 상세 실행 로그는 로컬 `build/author-install.log`, `build/society-regression.log`, `build/author-artifact-audit.json`에 있다.


## 공통 파일 기여 기록과 영구 편집자 명단

`Authorship`은 iiCSMIDI·iiGeneralDocument·iiSharedCanvas의 파일 메타데이터에 공통으로 쓰는 값 객체이다. `setAuthor()`는 host가 제공한 FileAuthor의 공개 JSON만 복사하고 변경된 프로필을 즉시 기록한다. `recordChange()`는 성공한 실제 변경마다 revision과 기여 시각을 갱신하고 즉시 compact JSON을 생성한다. `dump()`는 미리 갱신된 바이트를 반환하며 타이머·save·소멸자를 기다리지 않는다. 같은 프로필 선택은 revision을 바꾸지 않으며 활성 편집자 문맥만 설정한다.

최초로 성공한 `setAuthor()`가 최초 편집자 한 명을 기록한다. 이후 처음 등장한 계정은 첫 등록 순서대로 참여자 목록 끝에 추가한다. `firstEditor()`는 `std::optional<FileAuthor>`, `participants()`는 최초 편집자를 제외한 `QList<FileAuthor>` 복사본을 반환한다. 아직 작성자를 등록하지 않은 파일은 최초 편집자가 없고 참여자는 빈 목록이다. 작성자 미지정 변경이나 파일 읽기로 신원을 추론하지 않는다.

한 번 기록된 계정과 역할은 같은 `Authorship`의 모든 편집 API에서 유지되며 삭제·초기화·역할 교체 API가 없다. 명단의 내부 기준은 추가만 가능한 단일 `authors` 목록이다. 그 첫 항목이 최초 편집자이고 나머지가 참여자이므로 역할 목록이 서로 어긋나지 않는다. 동일 계정 재선택·프로필 갱신·활성 편집자 해제·시계 역행·직렬화와 복원에도 최초 편집자와 참여자 순서·최초 기여 시각을 보존한다. 프로필 갱신은 표시 정보와 최근 기여 시각을 갱신하며, 신원이 달라지면 기존 신원을 남기고 새 참여자로 추가한다. 반환된 프로필이나 JSON 복사본을 수정해도 원본 명단은 바뀌지 않는다.

저장 키는 `iisacc:authorship`, 현재 0.4.0의 스키마는 아래의 필드를 가진다. 작성자 프로필 내부의 `FileAuthor::SchemaVersion`은 기존 1을 유지한다. 파일 자체의 이름·URL 쌍은 [파일 링크 계약](FILE_LINKS.md)에 정의한다.

| 필드 | 저장 의미 |
| --- | --- |
| `schemaVersion` | 숫자 `3` (`Authorship::SchemaVersion`) |
| `revision` | unsigned decimal 문자열 |
| `modifiedAt` | 최신 UTC 기여 시각, 빈 이력에서는 null |
| `authors` | 등록 순서의 `{key, author, firstChangedAt, lastChangedAt}` 목록 |
| `firstEditor` | `authors` 첫 항목의 key. 명단이 비어 있으면 null |
| `participants` | `authors`의 두 번째 항목부터 끝까지의 key 목록. 최초 편집자는 제외 |
| `lastAuthor` | 최근 변경의 계정 key 또는 작성자 미지정 변경의 null |
| `links` | 선택적인 파일 링크 `{name, url}` 목록. 링크가 없으면 빈 배열 |

key는 정규화한 서비스 origin과 sub를 LF로 이어 붙인 값의 SHA-256이며 author는 FileAuthor JSON이다. 이메일·표시 이름이 같아도 sub 또는 서비스가 다르면 별도 참여자이고, 동일 origin/sub의 프로필 변경은 중복을 만들지 않는다. `firstEditor`와 `participants`는 프로필을 중복 저장하지 않고 `authors`를 참조한다. 스키마 2·3 읽기는 두 필드를 필수로 검사하며 최초 편집자 교체·최초 편집자의 참여자 중복·누락/중복/알 수 없는 참여자·순서 불일치를 거절한다.

스키마 1·2는 각 버전의 기존 필드만 허용하는 엄격한 검사를 유지한다. 최초 등록 순서였던 `authors`의 첫 항목과 나머지 항목을 그대로 구분하고 빈 링크 목록을 추가하여 스키마 3으로 복원한다. 같은 기여 시각이나 시계 역행을 이유로 정렬하지 않으며, revision·프로필·기여 시각을 바꾸지 않고 활성 편집자도 설정하지 않는다. 이후 덤프는 스키마 3이며 확장된 `MaximumBytes`와 Authorship 값 배치도 반영해야 하므로, 소비자는 iiFileProvider 0.4.0 이상의 헤더·라이브러리로 다시 빌드해야 한다. 공유 라이브러리 ABI 식별자는 `0.4`이다. 개별 파일 형식의 외부 메타데이터 봉투 버전과 이 JSON의 스키마 버전은 별개이다.

인원 한도는 최초 편집자를 포함한 최대 256명이다. 이전 입력 한도는 스키마 1의 compact UTF-8 JSON 1 MiB, 스키마 2의 1 MiB + 32 KiB를 유지한다. 현재 스키마 3의 `MaximumBytes`는 선택적인 링크 목록을 포함하여 2 MiB이다. 기존 한도까지 채운 파일도 전체 명단을 보존하며, 기존 항목을 생략하는 마이그레이션은 하지 않는다. 한도를 넘는 새 등록이나 프로필 갱신은 `std::length_error`로 거절하며 이전 항목을 지우거나 명단을 잘라내지 않는다. 잘못된 시각과 revision 소진도 변경을 원자적으로 거절하며 활성 편집자·기존 덤프를 보존한다. 알 수 없는 필드·중복 ID·잘못된 참조나 시각은 읽기에서 거절한다. 시계가 뒤로 가도 최신 수정 시각은 감소하지 않는다.

FileAuthor의 런타임 토큰·로그인 세션·활성 편집자 문맥은 덤프와 작성자 조회 복사본에 없다. 명시적으로 입력한 파일 링크 URL의 query/userinfo는 URL의 일부로 보존한다. `clearActiveAuthor()`는 현재 편집 문맥만 해제하며 영구 명단과 파일 링크는 변경하지 않는다. 파일 읽기는 active author를 비우므로 이전 작성자의 신원을 새 편집에 빌려 쓰지 않는다. host가 작성자를 지정하지 않은 변경은 lastAuthor:null로 기록한다. 첫 기여 시각은 파일의 원래 생성 시각으로 추론하지 않는다. 여러 작업을 하나의 파일 트랜잭션으로 묶을 때 실패·no-op 여부를 판정한 뒤 recordChange를 호출하며, 저장에 실패하면 문서와 이 값 객체를 함께 롤백해야 한다.

이 값 객체의 보존 계약은 호스트가 같은 파일의 이력을 이어 사용하고 덤프를 저장·복원하는 경로에 적용된다. 복사·대입은 트랜잭션 롤백을 위해 유지한다. 외부에서 파일 메타데이터를 삭제하거나 별도의 빈/오래된 `Authorship` 값으로 교체하는 행위를 막는 변조 방지 기능이나 여러 이력의 병합 기능은 아니다. 호스트는 성공한 기존 편집의 명단을 일반 내용 되돌리기로 교체하지 않아야 한다.

`tests/authorship.cpp`와 설치 소비자는 즉시 덤프·기여자 선택·JSON 왕복·토큰 제외·타입 오류·시계 역행·실패 원자성에 더해, 최초 편집자 고정·참여자 순서·동일 계정 중복 방지·프로필 갱신·서비스별 신원 구분·조회 복사본 격리·실제 메타데이터 파일의 저장/재열기·스키마 1 호환·잘못된 역할 참조·인원 및 용량 한도에서 전체 명단 보존을 검증한다. 영구 명단은 기존 Qt Core의 JSON·목록·해시와 FileAuthor 검증을 재사용하는 파일 도메인 규칙이므로 신규 외부 라이브러리나 상위 앱 의존성을 추가하지 않았다.

2026-09-08 macOS arm64 / Qt 6.8.3에서 0.3.0 Release 빌드, 소스 CTest 3/3 및 `build/stage/` 설치 패키지만 사용하는 별도 소비자 CTest 3/3이 통과했다. 소스·설치본의 Authorship Qt Test는 각각 38 passed(초기화/정리 포함)이며, 정확히 1 MiB인 스키마 1 파일의 256명 전체 복원도 포함한다. 공개 헤더·문서의 설치본 일치와 두 조회 API의 공유 라이브러리 export를 확인했다. 실행 로그는 `build/editor-roster-install.log`와 `build/editor-roster-installed-files.json`이다.

기존 iiCSMIDI·iiGeneralDocument의 Authorship, iiSharedCanvas의 Authorship·IiscCodec·DocumentFile·AudioPersistence 실행 파일 6개도 0.3.0 라이브러리를 로드하여 모두 통과했다. 이 검사는 기존 실행 파일의 런타임 회귀이며, 소비자 전체의 재빌드나 배포를 뜻하지 않는다. 각 프로세스가 로드한 `build/stage/lib/libiiFileProvider.0.3.0.dylib` 경로와 결과는 `build/editor-roster-consumer-regression.json`에 기록했다.

## 서버 모델과의 대응 검증

서버 문서 `docs/ACCOUNT_AUTHORS.md` 및 합성 샘플 `docs/contracts/author-account.json`과 함께 관리한다.
SDK 샘플은 `tests/fixtures/iisacc-account.json`이다. 필드명·길이·타입·UTF-8 크기 한도 변경 시 양쪽의
정규화·부분 수정·토큰 제외·왕복 검증을 같이 갱신한다. 서버에서 빠진 작성자 필드는 기존 값 유지,
빈 문자열/배열은 명시적 제거이다. SDK export는 모든 편집 필드를 담으므로 전체 프로필 갱신에 해당한다.
계정 업데이트가 기존 파일이나 활성 작성자를 자동으로 덮어쓰지 않으며, 호스트가 새 스냅샷을 적용한다.

GraphQL 응답의 `data.appSession` 또는 `data.accountSession`을 호출 측에서 먼저 해제한다. 이 라이브러리는 HTTP 호출을 수행하지 않으며 기존 `{account, session, ...}` 도메인 JSON을 읽는다.
