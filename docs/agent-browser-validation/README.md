# Yee Agent 검증 자료

이 디렉터리는 현재 판정, 기계 판독 집계와 그 근거를 연결한다. 숫자와 상태를
여러 색인 문서에 복사하지 않고 아래 결과 문서 한 곳에서 확인한다.

- [현재 결과와 남길 교훈](results.md)
- [기계 판독 비교](comparison.json)
- [증거·해시 색인](evidence-index.json)
- [검증 기준](../agent-browser-gate.md)
- [전사 형식](../agent-browser-transcript-format.md)
- [UI 자동화 운영](../ui-automation-session.md)
- [다음 작업](../agent-browser-worklist.md)

날짜별 중간 보고서와 단일 표본은 현재 결론으로 사용하지 않는다. 재현에 필요한
이전 후보의 비용과 실패는 `comparison.json`에만 유지한다.
`candidate.app`, `source_record`처럼 JSON에 기록된 절대 경로는 당시 캡처 위치를
식별하는 provenance이며 현재 실행 경로나 보존 파일 링크가 아니다.

`evidence-index.json`에서 `docs/`와 `tools/`를 가리키는 record는 clean checkout에도
남는 추적 자료다. `.local-build/agent-validation/`을 가리키는 record는 Git에서
제외한 로컬 원본 cache이므로 clean checkout이나 cache 정리 뒤에는 존재하지 않는다.
이 record의 경로·크기·SHA-256은 로컬 파일이 있을 때 동일한 캡처인지 확인하기 위한
값이며 저장소가 해당 원본을 보존한다는 뜻이 아니다. 저장소에 남는 현재 판정은
`results.md`, `comparison.json`, native prompt 자료와 추적된 runner/test를 기준으로
읽는다.
