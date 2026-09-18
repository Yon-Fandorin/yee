# Diagnostic transcript and matched-settings audit

`measure-browser-transcript.py` reads one JSON run per line. Required fields are
`run_id`, `harness`, `version`, `task_id`, `model`, `viewport`, `success`, `events`.
Each event is `{ "kind": "instruction|tool_call|tool_result|assistant", "text": "..." }`.
All attempts, including failures, belong in the input. Run IDs must be unique.

Optional `provider_usage` contains reported usage entries with `input_tokens`,
`output_tokens`, `cached_input_tokens`, and `reasoning_tokens`. Missing values
stay unknown, not zero. Normalize provider-specific meanings before supplying
entries: input includes cached input and output includes reasoning. Detail
fields are not added again. This schema does **not** yet prove that every model
call is captured; `provider_usage_complete` only describes reported fields.

Optional `comparison` has exactly these keys:

```json
{
  "cohort": "fixture-suite-v1",
  "provider": "provider-and-endpoint-identity",
  "reasoning": "canonical-reasoning-and-generation-settings",
  "initial_state": "fixture-and-reset-state-digest",
  "approval_policy": "explicit-shared-policy-version",
  "repetition": 1
}
```

For current comparisons use the same cohort/task/repetition for `yee` and `aside`.
The diagnostic format also accepts historical `codex-browser-use` and `browser-use`
records; they do not require new connection recovery or tests. Current scope and
reuse rules are in the [validation gate](agent-browser-gate.md).
Record the exact model identifier and inner
viewport, not merely a family name and browser-window dimensions. Keep each
harness version pinned throughout a cohort. Legacy records without comparison
metadata remain measurable but unmatched. A settings match is **not** a claim
that the metadata is truthful, provider accounting is complete, success was
independently verified, or Yee is better. The report never emits competitive PASS.

Group costs include failed attempts. Cost per success divides all attempted-run
cost by successful-run count; zero successes or unknown provider cost produces
null. Text-token accounting encodes event texts independently with pinned
tiktoken and excludes chat framing/context replay. Raw model requests/responses,
call IDs, retries, image accounting and external success evidence remain required
before full-task provider comparison. Do not use local text totals as a fallback
winner metric when any harness lacks provider records.
