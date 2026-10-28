# Contribution workflow

Follow this workflow alongside [`AGENTS.md`](AGENTS.md). The user's latest
instructions take precedence.

## 1. Ground the task

- Define the expected behavior, current symptoms, scope, and acceptance criteria.
- Read applicable specs, repository rules, existing code/tests, and Git changes.
- Preserve user changes and other tasks' work. Keep unrelated cleanup out of scope.
- Reuse approved decisions and architecture. Ask only to resolve behavior or
  ownership conflicts, or obtain information needed to proceed.

## 2. Apply both skills

- Use `codebase-architecture` to check ownership, placement, wiring, and dependency
  direction. Retain existing structure unless a change is justified.
- Use `codewright` for scoped implementation, readability, tests, documentation,
  and completion criteria.
- Follow each skill's installed `SKILL.md`. Apply repository rules and
  [`.codewright.toml`](.codewright.toml) for language and structure approval.

## 3. Implement and check

- Make scoped changes and update affected tests and documentation.
- Respect Yee/Chromium ownership. Mirror edits to Chromium originals into the
  repository patches specified by `AGENTS.md`; external checkout edits alone are incomplete.
- Use existing syntax, formatting, compilation, and regression checks as applicable.
  See [development tools](tools/dev/README.md) and [tests](tests/README.md).
- Start with focused checks, then run required validation tiers. Group related
  fixes in the same execution path to avoid redundant large builds.

## 4. Review with fresh context

Use a separate agent without inherited implementation conversation. Self-review
does not count as independent review. Provide:

- Requirements, approved decisions, and acceptance criteria.
- Repository rules, scope, relevant files, and the final diff.
- Check results and unverified areas.

The reviewer independently checks code and wiring for errors, omissions,
regressions, ownership, placement, and test adequacy. Review is read-only by
default; coordinate any edits, builds, app launches, or installations.
Report actionable findings or explicitly state none, including review limits.

## 5. Fix and re-review

- Fix findings, rerun affected checks, and obtain another fresh-context review
  of the updated final state.
- Repeat until known issues in scope are resolved and final review has no
  actionable findings. Re-review code changed after review.
- Report unverified items, why they remain unverified, and remaining work.
  Do not claim required validation passed when it was not performed.

## 6. Validate in Yee

After review, validate runtime changes such as UI, ad blocking, or installation
in the newly built app.

- Identify the build executable and isolated validation profile before launching.
  Request graceful shutdown only for processes using that executable or profile,
  then wait for those processes to exit. Keep an installed release Yee running
  when its executable and profile are separate. Report conflicting processes that
  remain; force-stop only with explicit user authorization. Preserve unrelated
  desktop windows when invoking native test launchers.
- Verify the reviewed source, running build, and installer under test represent
  the same changes. Existing processes or older installations are not evidence
  for a new build.
- Exercise real tabs and user scenarios. Record screenshots, logs, versions, and
  results as appropriate; mockups or source checks alone do not prove runtime behavior.
- Isolate test profiles and preserve existing user settings and data. Delete only
  data verified as created by the current task.
- If runtime validation finds a problem, repeat implementation, affected checks,
  independent review, and affected runtime validation.

For documentation-only or other non-runtime changes, check links, consistency,
and the diff as applicable. App builds and launches are not required.

## 7. Report and deliver

Report changes, verification, and remaining limits or unverified areas. Distinguish
passed, failed, and skipped checks, and review results from runtime evidence.
Do not combine separate runs into a claimed single end-to-end pass.

Commit or push only when requested or authorized for this task, after resolving
known issues and completing required checks, final independent review, and
applicable runtime validation. Include only this task's changes. When a push is
requested, verify the remote update and report the commit and outcome.
