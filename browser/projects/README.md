# 프로젝트와 페이지 참조

일반 프로필의 프로젝트 이름·설명·페이지 순서와 마지막 유효한 HTTP(S) URL·제목을
`yee.projects.state` Pref에 저장한다. 페이지 본문·폼·AI 대화는 저장하지 않는다.
비공개·게스트·시스템 프로필에서는 서비스를 만들지 않는다.

`ProjectService`는 UUID와 저장 규칙을 소유하며 Views나 탭 모델에 의존하지 않는다.
`ProjectTabCoordinator`는 UUID를 현재 `TabInterface`에 연결하고, 해당 탭의 현재
WebContents를 창별로 조회한다. 자료 목록은 공유하지만 각 창은 독립된 native 탭을
열고, 현재 창에 이미 열린 항목만 재사용한다. 내용 교체 때 `ProjectTabHelper`의 UUID와 관찰을 넘긴다.
닫기 취소 여부가 정해지기 전의 `WillDetach` 알림으로 연결을 제거하지 않는다.

창의 허브와 탐색 UI는 [`browser/ui/projects/`](../ui/projects/)에 있다.
허브는 실제 탭을 탐색시키지 않는 native Views 화면이다. 프로젝트 참조 삭제는
실제 탭이나 Chromium의 방문 기록을 삭제하지 않는다. 일반 탭 복제는 프로젝트를
상속하지 않으며, 같은 URL의 서로 다른 탭은 서로 다른 항목이다.

Create 메뉴에서 이름과 선택 설명을 입력해 프로젝트를 만든다. 허브의 ‘현재 탭 추가’가
실제 HTTP(S) 탭을 연결한다. 세로 프로젝트 행은 일반 그룹처럼 목록을 접고 펼친다.
오른쪽 허브 버튼이나 수평 Header의 이름은 허브를 열고 더보기는 프로젝트 메뉴를 연다.
허브 진입·복귀는 폴딩 상태를 유지하며 상단에는 프로젝트 이름·전환만 표시한다. 다른 프로젝트에 연결된 탭은 기존/새 프로젝트를
표시하는 확인 창에서 이동을 선택해야 소속을 바꾼다.

세션 추가 데이터 `yee.project_page_id`는 일반 저장, 세션 재구축, 정상 복원,
닫은 탭 복원에 연결한다. 브라우저 시작 설정은 변경하지 않는다. 이전 세션을
복원하지 않았다면 항목은 저장 상태로 남고 선택할 때 새 native 탭을 연다.
창마다 같은 자료 UUID를 복원할 수 있다. 같은 창 안의 중복 UUID만 해제하며,
같은 자료가 있는 창으로 이동한 탭은 실제 내용을 보존한 일반 탭으로 남긴다.
알 수 없는 저장 버전이나 손상된 참조는 원본을 보존하고 편집을 막는다.

## 검증

Chromium 소스를 동기화한 뒤 빌드 출력 디렉터리에 맞춰 실행한다.

```sh
autoninja -C out/YeePilot chrome/browser/yee_projects:project_service_unittests
out/YeePilot/project_service_unittests
autoninja -C out/YeePilot chrome/browser/ui/views/yee:site_controls_browsertests
out/YeePilot/site_controls_browsertests --gtest_filter='ProjectNavigationBrowserTest.*' --test-launcher-jobs=1
```

실제 앱에서는 프로젝트 생성·현재 탭 추가·두 배치의 목록·허브 왕복·다른 프로젝트로
이동·탭 닫기 취소·정상 재시작을 확인한다. 새 코드의 검토·전체 앱 빌드와 정상 종료 후
새 프로세스 검증은 저장소의 [기여 절차](../../CONTRIBUTING.md)를 따른다.
