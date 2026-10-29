# 파일 입출력과 안전 저장의 직접 소유

`iiFileProvider::FileDevice`와 `SaveFile`은 QFile/QSaveFile 파일 엔진을 호출하거나
상속하지 않는다. `TemporaryFile`도 QTemporaryFile을 상속하지 않는다.
C++23 `iiFileProvider::native::Device`, `AtomicSave`, `publishFile`,
`renameFileExclusive`가 운영체제 파일 동작을 소유한다. Native는 Qt와 다른 iiSDK
기능 모듈에 의존하지 않는다. 기존 Qt 코덱용 계층은 QIODevice/QString 호환 어댑터이다.
QtCore 전체의 제거와 이번 파일 엔진 제거는 별개의 변경이다.

## 네이티브 API

`<iiFileProviderNativeDevice.h>`를 포함하고 `iiFileProvider::Native`에 링크한다.

```cpp
namespace fp = iiFileProvider::native;
fp::Device file;
file.open(path, {.read=true, .write=true, .existing=true});
file.seek(0);
file.write("data", 4);
file.flush();
file.close();

fp::AtomicSave save;
save.open(path);
save.device().write("complete", 8);
const auto published = save.commit();
if (!published.succeeded) throw std::runtime_error(published.message);
```

- Device는 Windows의 네이티브 핸들과 CRT descriptor, POSIX descriptor를 소유한다.
  append, 기존 파일 요구, 배타적 생성, truncate, seek, resize와 권한을 제공한다.
- read/write/open/seek/close는 기존 SourceAccess 계약과 조정한다. 새 파일은 생성 후
  식별자를 연결한다. 열려 있으나 읽지 않는 장치가 잠금을 계속 점유하지 않는다.
- int descriptor와 FILE*는 기본적으로 빌려 쓴다. 명시한 takeOwnership만 닫는다.
  빌린 핸들의 경로 기반 조정이 필요하면 호출자가 SourceAccess 범위를 소유한다.
- map/unmap은 정렬되지 않은 파일 내 구간과 private copy를 지원한다. 매핑은 닫을 때
  해제된다. 매핑 주소에 직접 접근하는 기간의 스레드·문서 조정은 호출자의 책임이다.
- 일반 flush는 라이브러리 버퍼를 처리한다. flush(true)는 파일의 디스크 동기화를
  요청한다. AtomicSave는 파일 동기화와 descriptor 닫기에 성공한 뒤 게시한다.

## 안전 저장 계약

AtomicSave는 부모 디렉터리를 임의로 생성하지 않는다. 같은 디렉터리에 배타적으로
0600 임시 파일을 확보하며 TemporaryLease로 활성 스테이지를 정리 작업에서 보호한다.
임시 경로에는 최종 파일 확장자를 보존한다. 성공한 commit만 대상 경로를 교체한다.
취소, 미완료 객체 소멸, 쓰기·flush·close 실패, 닫힌 출력의 commit은 스테이지를
폐기하고 기존 파일을 유지한다. 중복 commit은 실패한다. 심볼릭 링크와 디렉터리를
저장 대상으로 허용하지 않는다. Native AtomicSave에는 직접 쓰기 fallback이 없다.

기존 파일 권한은 기본으로 보존하고 명시적인 setPermissions를 우선한다. 새 파일은
0666과 실제 umask로 정해지는 보통 파일 권한을 사용한다. 이를 위해 프로세스 umask를
변경하지 않고 짧게 생성한 전용 probe에서 결과를 읽는다. 대상이 이미 있으면
commit(false)는 교체하지 않는다. 운영체제의 rename/no-replace 또는 독점 link 게시를
사용하며 원자적 게시 실패를 일반 copy로 숨기지 않는다. 디렉터리 fsync를 포함한
전원 장애 후의 영속성 보증까지 이 API가 추가로 주장하지 않는다.

파일 rename은 별도 계약이다. 같은 파일시스템에서 배타적 rename을 사용하고,
파일시스템이 다른 경우 완전한 복사와 배타적 게시 후 원본을 제거한다. 이 이동은
원자적 단일 rename이 아니다. 게시 후 원본 정리 실패는 명시적으로 실패를 반환하고
다른 프로세스가 변경했을 수 있는 대상 파일을 임의로 삭제하지 않는다.

## Qt 소비자

FileDevice는 QIODevice를 직접 상속한다. 오류 코드·권한 플래그·파일 이름,
추가 쓰기·배타적 생성·기존 파일 열기·위치 이동·크기 변경·메모리 매핑을 제공한다.
QResource의 읽기 전용 `:/` 데이터도 파일 엔진 없이 어댑터에서 처리한다.
QImage, QCryptographicHash, QDataStream 등 QIODevice 소비자는 이 장치를 사용할 수 있다.

SaveFile은 네이티브 AtomicSave에 연결하고 close/destructor로 게시하지 않는다.
commit을 명시해야 한다. cancelWriting과 닫힌 저장 장치의 resize는 기존 파일을
변경하지 않는다. `setDirectWriteFallback(true)`를 명시한 호환 소비자만 임시 파일을
만들 수 없는 경우 직접 쓰기를 허용한다. 이 경우 취소가 원본을 복구하지 않는다는
계약도 유지한다. 기본값은 false이다.

Android content URI는 명시적으로 허용된 직접 쓰기에서만 ContentResolver의
ParcelFileDescriptor를 받아 provider-owned descriptor로 처리한다. Qt 파일 엔진이나
가짜 로컬 경로로 우회하지 않는다. URI의 최종 원자성·저장 완료 의미는 해당 문서
제공자의 계약을 따른다. 실제 Android 실행은 이번 검증에 포함하지 않는다.

Qt 값 타입, QIODevice, QResource, QLockFile, 기존 디렉터리 호환 클래스는 남아 있다.
QFile/QSaveFile로의 포인터 변환과 Qt 파일 엔진 플러그인 호환성을 제공하지 않는다.
권한 플래그는 소유자·그룹·기타 POSIX 모드에 대응하며 ACL 권한 확인의 대체물이 아니다.

## 패키지와 검증

Qt 어댑터는 0.8.0 / SONAME 0.8이다. 상속과 객체 배치가 바뀌므로 기존 SONAME 0.4
바이너리와 혼용하지 않으며 소비자는 새 헤더로 다시 빌드해야 한다. Native는 0.10.0이며
기존 Native API는 유지하고 새 API를 추가한다. Qt 패키지는 Native 0.10.0 이상을 요구한다.
검증 설치는 build/qfile-removal-20261027 아래의 prefix에서 진행하며 실제 사용자
컨테이너나 전역 설치본을 마이그레이션하지 않는다.

Native Device 테스트는 이진·Unicode 경로, append/seek, 배타적 생성, 크기 변경,
private map, 취소·소멸·실패 저장, no-replace 게시·rename, 빌린 stream 소유권을
검사한다. Qt 장치 테스트는 실제 QResource, 이미지 인코더, 읽기 후 쓰기 위치,
임시 파일 재열기와 QIODevice 호환성을 검사한다. 기존 저널·RAW·온톨로지·DB·목록
검사를 유지한다. dependency_direction은 생산 소스에 QFile/QSaveFile/QTemporaryFile
참조가 재도입되거나 Native가 Qt를 포함하는 경우 실패한다. 설치 소비자와 타겟 교차
컴파일은 실제 타겟 실행과 구분한다. 최종 결과는 같은 build 폴더의 verification.json에
기록한다.

### 2026-10-27 검증 결과

최종 소스로 macOS arm64의 Qt 어댑터와 Native를 다시 빌드했다. 아래 검사는 서로
겹치는 항목을 포함하므로 합산한 고유 테스트 수로 해석하지 않는다.

| 검사 | 결과 | 증거 |
| --- | --- | --- |
| 최종 변경 관련 Qt·Native 회귀 검사 | 12/12 통과 | `qt-verified-tests.xml` |
| Qt 없이 구성한 Native 전체 검사 | 6/6 통과 | `native-verified-tests.xml` |
| AddressSanitizer·UndefinedBehaviorSanitizer 장치·접근 조정 검사 | 2/2 통과 | `sanitized-verified-tests.xml` |
| 설치 위치를 옮긴 Native 소비자 | 2/2 통과 | `native-consumer-verified-tests.xml` |
| 설치 위치를 옮긴 Qt 소비자 | 8/8 통과 | `qt-consumer-verified-tests.xml` |
| 전체 회귀 검사 체크포인트 | 47/48 통과 | `qt-tests.xml` |

전체 검사에서 실패한 `iiFileProvider.workspace_creation`은 워크스페이스 파일 생성
감사이다. iiActorObject의 예제 네 파일과 iiVisualCodec의 예제·USD 구현 두 파일에서
provider 밖의 생성·복사 경로 12건을 보고했다. 해당 외부 모듈을 이번 파일 엔진
교체에 포함하지 않았으며 감사의 조건·제한 시간·허용 목록도 완화하지 않았다.
상세 경로는 `workspace-audit.json`에 있다. 전체 검사 이후의 최종 변경에는 위의
변경 관련 12개와 Native·sanitizer·설치 소비자 검사를 다시 수행했다.

Windows x86_64, Linux aarch64, Android arm64, iOS arm64, Web/WASM에 대해
`Device.cpp`와 `AtomicSave.cpp`의 오브젝트 10개를 교차 컴파일했다.
Android Qt 어댑터의 실제 JNI/content URI 분기도 NDK와 Qt Android 헤더로 컴파일했다.
교차 컴파일 기록은 최종 소스 해시와 일치한다. 이는 해당 소스의 타겟 컴파일 증거이며
전체 SDK의 타겟 링크나 타겟 운영체제에서의 실행 검증을 의미하지 않는다.

설치된 Qt 어댑터의 미해결 심볼에는 QFile/QSaveFile/QTemporaryFile/QFileDevice
메서드가 없고 Native의 동적 링크에는 Qt가 없다. 설치 소비자는 저작 소스 디렉터리의
헤더를 포함하지 않았으며, 동적 로더 기록에서도 `relocated/lib`의 새 라이브러리를
실제로 사용했다. 전역 `~/.local` 설치본은 교체하지 않았고 실제 Society 데이터나
컨테이너는 수정하지 않았다. 이번 검증에서 GUI 앱을 실행하지 않았다.

참조 계약: [Qt 파일 장치](https://doc.qt.io/qt-6.8/qfile.html),
[Qt 안전 저장](https://doc.qt.io/qt-6.8/qsavefile.html),
[Qt QIODevice](https://doc.qt.io/qt-6.8/qiodevice.html).
