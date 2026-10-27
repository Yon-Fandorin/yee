# 프로젝트 탐색 UX 참고 자료

2026-10-26 대화에서 정리한 혼합 목록·프로젝트 기본 화면을 보관한다.
작업을 이어갈 때는 [핸드오프](../../../docs/project-navigation-handoff.md)를 먼저 읽는다.
이 폴더는 화면 시연이며 native 기능이나 제품 명세를 대체하지 않는다.

## 열기

[index.html](index.html)은 독립 실행용으로 내보낸 시안이다. 로컬 HTTP로 열려면
이 브랜치의 저장소 루트에서 다음과 같이 실행한다.

```sh
python3 -m http.server 4173 --bind 127.0.0.1
```

브라우저에서 `http://127.0.0.1:4173/experiments/shell-prototype/project-navigation/`을 연다.
Windows에서는 설치된 Python 실행 명령으로 바꿔 실행할 수 있다.
하단 선택기로 수평·세로 배치를 전환하고 프로젝트 이름, 옆 화살표, 항목을 눌러 본다.

## 파일

| 파일 | 내용 |
| --- | --- |
| [ux-fragment.html](ux-fragment.html) | 대화에서 사용한 원본 HTML 조각. 제품 코드로 복사할 대상이 아니다. |
| [index.html](index.html) | 원본을 감싸 내보낸 실행용 화면. 이 복사본을 수정해 제품 구현으로 간주하지 않는다. |
| [manifest.json](manifest.json) | 원본·내보낸 화면·캡처의 해시와 검증 범위 |

현재 브랜치는 `pilot`의 `7966cf8` 위로 리베이스했으며 당시 13개 UI 파일은 기반에
포함돼 있다. 불필요해진 당시 파일 해시 목록은 정리하고 원본 시안과 UX 캡처는 유지했다.
Windows 실제 창 40개 사례는 통과했다. 빌드·단위 검사 결과, 실제 창 검수의 범위와
맥 동기화 절차는 [핸드오프](../../../docs/project-navigation-handoff.md)에 있다.

원본은 Codex visualize 1.0.55의 `scripts/render.py`로 내보냈다.
내보낸 화면은 이 저장소 안에서 열 수 있으며 원래 대화의 절대 경로를 읽지 않는다.
렌더러가 사용하는 외부 아이콘 리소스 등은 실행 환경의 네트워크 상태에 영향을 받을 수 있다.
이미지 캡처는 해당 리소스를 다시 불러오지 않고 읽을 수 있다.

## 캡처

모두 같은 가상 프로젝트를 보여 주는 HTML 시안 캡처다. 실제 Yee 앱의 검증 자료가 아니다.

| 화면 | 캡처 |
| --- | --- |
| 세로 탭·왼쪽 혼합 탐색·프로젝트 기본 화면 | [vertical-hub.jpg](captures/vertical-hub.jpg) |
| 왼쪽 영역 없는 수평 탭·프로젝트 기본 화면 | [horizontal-hub.jpg](captures/horizontal-hub.jpg) |
| 수평 웹탭과 프로젝트 혼합 목록 | [horizontal-mixed-list.jpg](captures/horizontal-mixed-list.jpg) |
| 허브를 유지하며 대화 패널 열기 | [vertical-conversation.jpg](captures/vertical-conversation.jpg) |

캡처는 JPEG 전체 페이지 이미지이며 편집·합성하지 않았다. 프로젝트 제목에 들어 있는
‘Yee 리디자인’은 예시 이름이다. 창 상단의 별도 `yee` 브랜드 표시는 제거된 상태다.

## 시연 범위

프로젝트 이름은 기본 화면, 옆 화살표는 항목 목록으로 연결한다. 일반 탭·일반 그룹과
프로젝트가 함께 있고, 프로젝트 목록은 페이지·대화·작업을 아이콘으로 구분한다.
대화·작업은 기본 화면 또는 현재 페이지 옆에서 연다. 실제 AI 호출은 실행하지 않는다.
일부 UI 상태를 기억하지만 대화 본문이나 실제 브라우저 세션의 저장·복원 구현은 아니다.
