# 내부 페이지

[전체 주소 목록](internal-pages-inventory.md)은 등록된 WebUI와 설정 세부 경로를,
[리디자인 후보](internal-pages-redesign.md)는 사용자 화면의 이관 우선순위를 정리한다.
이 문서는 구현한 화면의 주소·소유 경계·동작을 설명한다.

## 주소

| 사용자 주소 | 실제 WebUI | 원래 화면 |
| --- | --- | --- |
| `브랜드://settings` | `chrome://yee-settings/` | `chrome://settings/` |
| `브랜드://downloads` | `chrome://yee-downloads/` | `chrome://downloads/` |
| `브랜드://history` | `chrome://yee-history/` | `chrome://history/` |
| `브랜드://bookmarks` | `chrome://yee-bookmarks/` | `chrome://bookmarks/` |
| `브랜드://extensions` | `chrome://yee-extensions/` | `chrome://extensions/` |

브랜드 접두어는 `branding/brand.json`에서 생성한다. 실제 WebUI host와 저장 주소는
브랜드 변경에도 유지한다. 원래 Chromium 주소는 원래 화면을 열며 주소창 표시와
복사도 `chrome://`를 유지한다. 일반 설정·다운로드·방문 기록·북마크·확장 관리 메뉴는 제품
화면을 연다. 다른 기기의 탭 등 방문 기록의 세부 경로는 원래 Chromium 화면을
연다. `브랜드://history/syncedTabs`도 `chrome://history/syncedTabs`로 연결한다.
설정의 세부 기능은 [설정 문서](settings.md)를 따른다.

확장 관리의 제품 주소는 루트의 `?id=`·`?q=`를 사용한다. 단축키·개발자 진단·
확장 옵션 등 다른 경로와 쿼리는 원래 Chromium 화면으로 연결한다.

## 공통 프런트엔드

`browser/ui/webui/`는 정적 SvelteKit 빌드, 테마, 기본 컨트롤, 번역과
`WebUIDataSource` 리소스를 소유한다. 페이지의 native 연결과 정책은 각 기능의
디렉터리에 둔다. 프런트엔드의 페이지 전용 모델·컴포넌트는 해당 route에 둔다.

설정 route group의 이름은 사용자 주소에 나타나지 않는다. 다른 페이지는
`hooks.ts`의 `reroute`로 구현 host의 루트 주소를 해당 route에 연결한다.
`InternalPage`는 제목·도구 모음·스크롤 영역을, `SearchField`는 검색 입력과
지우기를 공유한다. 목록·폴더·삭제 범위와 native 연결은 기능 route가 소유한다.
버튼·체크박스·툴팁·메뉴·확인창·편집창·선택 입력은 공통 UI 컴포넌트를 사용한다.
외부 폰트·이미지·CDN은 사용하지 않는다.

목록·상세 화면 안의 이동은 SvelteKit의 shallow navigation을 사용한다.
현재 주소는 공통 `currentPageURL()`이 `page.shallow.url`과 `page.url`에서
읽는다. 검색어·선택 항목과 브라우저 뒤로·앞으로 이동은 이 주소를 따른다.

## 다운로드

`browser/ui/downloads/`의 controller는 Chromium `DownloadsUI`를 상속한다.
기존 controller type과 Mojo binder, `DownloadsDOMHandler`를 재사용하므로
목록·검색·갱신·추가 조회와 파일 작업은 기존 다운로드 관리자에서 처리한다.
별도의 다운로드 저장소를 만들지 않는다.

검색어는 `?q=`에 보존한다. 검색 중에는 전체 기록 삭제를 비활성화하며,
실행 취소는 복구할 수 있는 기록이 있을 때 표시한다.

Mojo JavaScript와 타입 선언은 Chromium 빌드 결과에서 가져온다. 타입을 별도로
복사해 관리하지 않으며, 런타임 모듈은 다운로드 화면에서만 불러온다. 모델이
callback을 받고 화면을 갱신하며 페이지가 닫히면 연결을 정리한다.

일반 파일에는 파일 열기·폴더에서 보기, 일시정지·재개·취소·재시도와 기록 삭제를
상태에 따라 표시한다. 기록 삭제와 실행 취소는 기존 handler를 사용한다. 기록
삭제 정책과 자녀 프로필의 제한도 기존 화면과 동일하게 적용한다.

파일 이름의 마지막 확장자에 따라 이미지·동영상·오디오·문서·스프레드시트·
프레젠테이션·압축·코드·앱 아이콘을 표시한다. 알 수 없거나 확장자가 없는 파일은
일반 파일 아이콘을 사용한다. 아이콘은 파일 내용을 검사하거나 안전성을 판정하지
않으며, 보안 확인이 필요한 파일에는 경고 아이콘을 우선 표시한다.
파일 이름·출처 주소·날짜와 아이콘 버튼의 부가 설명은 공통 툴팁을 사용한다.

위험 파일과 검사 중인 파일은 보안 확인 상태로 표시한다. `경고 확인`은 원래
다운로드 화면으로 이동해 기존 경고·검사·확인 절차를 진행한다. 제품 화면에서
검사나 확인을 건너뛰어 파일을 여는 동작은 제공하지 않는다.
일반 목록에서는 원래 화면으로 이동하는 링크를 표시하지 않는다.

## 방문 기록

`browser/ui/history/`의 controller는 Chromium `HistoryUI`를 상속하며 기존
controller type과 Mojo binder를 유지한다. 조회·검색·추가 조회·기록 삭제는
기존 history handler를 사용한다. 검색어는 `?q=`에 보존하고 오래된 응답은
현재 검색 결과를 덮어쓰지 않는다.

날짜별 방문을 표시하며 개별 또는 선택한 기록을 확인창에서 삭제한다. 같은
날짜에 묶인 방문은 native 응답에 포함된 모든 주소의 방문 시각을 함께 삭제한다.
기록 삭제 정책은 화면과 native 서비스에서 적용한다. 인터넷 사용 기록 삭제는
원래 Chromium 설정의 삭제 절차를 열고, 다른 기기의 탭은 원래 방문 기록의
해당 경로를 연다. 방문 주소는 기존 native 탐색 handler로 열어 클릭 수정키를
유지한다. favicon은 프로필의 `chrome://favicon2` source를 사용한다.

## 북마크

`browser/ui/bookmarks/`는 Chromium `BookmarksUI`를 상속한다. 기존
`BookmarksMessageHandler`, `chrome.bookmarks`와 `chrome.bookmarkManagerPrivate`
API를 사용하며 별도의 북마크 저장소를 만들지 않는다. API 허용 origin에 제품
WebUI를 추가하고 타입 선언은 Chromium 소유 파일에서 빌드 시 가져온다.

폴더 탐색·검색·북마크와 폴더 추가·수정·이동·확인 후 삭제·실행 취소를 제공한다.
현재 폴더는 native 주소 형식인 `?id=`로 보존하고 검색은 `?q=`로 보존한다.
계정과 기기의 저장소가 함께 있으면 폴더 탐색과 이동 목적지에서 구분한다.
폴더를 자신이나 하위 폴더로 옮길 수 없으며 관리되는 북마크와 최상위 폴더는
수정하지 않는다. 편집 정책 변경은 열린 화면에도 적용한다.

가져오기·내보내기와 새 탭 열기는 기존 private API를 사용하여 native 파일
선택기와 URL 탐색 정책을 유지한다. 모델 변경 event는 목록을 다시 조회하며,
페이지가 닫히면 event 구독과 타이머를 정리한다. 페이지 안의 폴더 탐색은
브라우저 Tab Sidebar의 예약된 북마크 영역을 활성화하지 않는다.

## 확장 프로그램

`browser/ui/extensions/`는 Chromium `ExtensionsUI`를 상속하고 기존
`chrome.developerPrivate`·`chrome.management` API를 사용한다. 설치 목록·
검색·사용 여부·상세 정보·사이트 접근·시크릿과 파일 접근 설정을 제공한다.
정책·보안 경고·변경 가능 여부는 native 응답을 따르며 별도 저장소를 만들지 않는다.

변경 알림은 하나의 상태 조회 흐름에 모은다. 조회 도중 새 알림이 오면 이전
응답을 버리고 최신 응답을 받은 뒤 조작을 다시 허용한다. 상세 화면은 확장 ID로
구분해 이전 확장 프로그램의 입력창이 다른 항목에 재사용되지 않도록 한다.
페이지가 닫히면 알림 구독과 예약된 조회를 정리한다.

전체 사이트 요청은 클릭·특정 사이트·전체 사이트 모드와 허용 사이트 목록으로,
유한한 사이트 요청은 전체 허용 스위치와 개별 사이트 스위치로 표시한다.
전체 허용 중에는 개별 스위치를 변경할 수 없다. 향상된 사이트 제어가 활성화된
빌드는 프로필 전체의 사이트 제한과 함께 처리하는 원래 관리 화면으로 연결한다.

삭제·재활성화와 확장 옵션은 기존 native API와 확인 절차를 사용한다.
단축키·개발자 기능·오류·복구는 원래 확장 관리 화면에서 처리한다. 게스트 제한을
유지하며 시크릿에서 확장 관리를 열면 원래 화면과 같이 일반 프로필로 이동한다.

## 검증

`InternalURLsTest`는 제품·원래 주소의 구분과 경로·쿼리·fragment 보존을 검사한다.
`DownloadsBrowserTest`는 실제 다운로드의 목록·검색·삭제·실행 취소, 일반 메뉴
연결과 native 기록 삭제 정책을 검사한다. 최종 화면 확인은 새로 빌드하고 다시
실행한 Yee 앱에서 진행한다. `HistoryBrowserTest`는 실제 history service를
사용한 검색·추가 조회·동일 날짜 방문 삭제·native 삭제 정책을 검사한다.
`BookmarksBrowserTest`는 실제 bookmark model을 사용한 편집·이동·삭제·실행
취소·native 주소 복원·관리되는 노드와 실시간 편집 정책을 검사한다.
`ExtensionsBrowserTest`는 실제 extension service로 목록·검색·상태 변경·
상세 주소 이동·사이트 권한·삭제 확인·관리 정책·게스트 제한과 기존 private API
동작을 검사한다.

`./tools/dev/test-internal-pages.sh`는 자동 기능 검사를 기본적으로 창 없이
실행한다. `--no-build`는 이미 빌드한 테스트 바이너리를 사용한다. 창이 필요한
디버깅은 `--ui=on`, 창 없는 실행은 `--ui=off`로 선택하며 `YEE_TEST_UI`로도
기본값을 정할 수 있다. 자동 검사 모드와 실제 앱의 화면 검수는 별도 절차다.
