# TASK — Historical backtest final acceptance

## Source of truth

The current working tree is the only implementation baseline. Do not look up or rely on commit hashes, old branches,
dated review notes, earlier archives or historical `TASK.md` files.

Read `AGENTS.md` before changing code. Preserve unrelated user changes.

## Accepted work

The following work has passed its implementation and owner review. Do not reopen or reimplement it:

- [x] TA-Lib pattern recognition, Analysis Events and closed-bar publication semantics.
- [x] Versioned strategy definitions, validation, JSON persistence and the Studio strategy editor.
- [x] Multi-series/timeframe resolution, derivation, warm-up and deterministic condition evaluation.
- [x] Provider-neutral strategy runtime, execution rules, audit evidence and `BacktestRunner`.
- [x] CLI strategy backtesting and deterministic offline coverage.
- [x] Gate 6B Backtest Run Setup and immutable request construction.
- [x] Gate 6C asynchronous Studio execution, progress, cancellation, terminal arbitration and worker cleanup.
- [x] Gate 6D historical results, chronological evidence, normal-chart navigation, persistent selectable overlays,
  zoom/pan/history behavior and independent manual indicators/profiles.
- [x] Gate 6D historical-result lifecycle: confirmed deletion of one terminal result and clearing all finished results,
  including confirmation messages and cleanup of owned overlays without deleting the definition or normal chart state.
- [x] Owner Windows verification of the Gate 6D workflow.

**Current and only authorized task: complete historical-backtest acceptance.**

## Accepted contracts

- A reusable `Strategy::Core::Definition` contains strategy logic, not a selected subject or backtest dates.
- A `BacktestRequest` is an immutable snapshot containing the definition, optional subject, inclusive `from`/`through`,
  provider configuration, quantity, optional starting capital, costs, slippage and fill-model version.
- Studio and the CLI execute the same request through the shared `Strategy::Core::BacktestRunner`.
- Provider planning, resolution, warm-up, derivation and engine behavior have one implementation.
- UTC ranges and closed/forming-bar semantics remain exact. Historical execution evaluates committed closed bars only.
- Historical backtests remain separate from polling/live strategy runs.
- Domain and Studio-core code remain independent from ImGui, GLFW and OpenGL.
- Worker code publishes immutable values only. Chart, workspace, ImGui and OpenGL mutation stays on the UI thread.
- Do not modify `extern/` or submodule pointers.

## Historical-backtest acceptance

This is an acceptance and observability gate, not a redesign. Reuse the existing runner, fixtures, Studio orchestration
and CLI. Make only the smallest corrections required by failed acceptance cases.

### Final audit finding — current submission

The owner has accepted the Windows interaction and presentation: repeated occurrences, independent overlay checkboxes,
translucent spans, hover, direct navigation, evidence blocks, result cleanup and default right-hand docking now match
the intended Pattern/Strategy workflow. The code audit found three remaining correctness/acceptance gaps:

- `Studio::Core::present()` still fills the unqualified Entry/Exit/P&L/return/cost/warning fields from
  `occurrences.front()`. With several occurrences this silently presents occurrence #1 as if it summarized the whole
  run. Remove that first-occurrence projection from the run summary, replace it with truthful aggregate fields, or label
  it explicitly as occurrence #1 without duplicating/misrepresenting the per-occurrence list.
- Evidence is retained only inside appended occurrences. `append_occurrence()` discards a final `NoSignal` engine,
  including evaluations after the last exit; a wholly no-signal run likewise has no outcome-level evaluation stream.
  Preserve chronological run-level evaluations for the entire requested range, assign an occurrence id only when they
  belong to an occurrence, expose unassigned items in the existing run-level evidence block, and serialize them in the
  versioned CLI report without fabricating a trade.
- The acceptance file exercises the right real paths, but several checked requirements are asserted only as non-empty.
  Add exact per-scenario projection/audit/cost expectations, exact repeated-occurrence projection and aggregate P/L
  expectations, and exact peer-evidence truth/source/timeframe/sequence-order assertions. Prove that evidence after the
  final exit reaches the end of the requested range. Keep the focused acceptance source reviewable.

- [x] Restore the repository's focused-file rule while touching this area: split the 531-line
  `studio/core/src/HistoricalResults.cpp` and 653-line `studio/core/tests/HistoricalBacktests.cpp` along coherent
  presentation/chart-integration or test subjects; do not grow existing large files further.

- Keep a focused historical-backtest acceptance test file and a clearly named CTest target. Keep the already large
  `EndToEnd.cpp` and `BacktestRunner.cpp` files from growing into the acceptance suite.
- Keep deterministic historical-backtest fixture documents in a domain-named fixture directory. Reuse shared helpers
  where practical, but make the scenario inputs and expected results easy to review.
- Drive every matrix row through the real `BacktestRunner`. Then drive the same request/provider data through the CLI
  application path and the Studio historical-backtest orchestrator, canonicalize only explicitly allowed presentation
  metadata, and compare the complete semantic result.
- Make timing absence truthful. A duration that was never measured, including total duration on a default/manual
  `BacktestOutcome`, must serialize as `null` and display as an em dash rather than fabricated `0.00 ms`.
- Start strategy-execution timing immediately around the engine work, excluding status/progress observer callbacks.
  Once a stage has started, preserve its elapsed partial duration when cancellation or failure exits that stage.
  Completed earlier stages must remain populated; stages that never started must remain absent.
- Add structural timing tests for a completed run, an early preparation failure and cancellation during execution.
  Preserve the reviewed timing field semantics when the plural-result report receives its required format-version bump.
- Strengthen the isolated scenario assertions. The current generic presence checks do not yet prove the exact expected
  time, price, ambiguity, costs, gross/net P/L, return and projection/audit values promised by the checked task item.

### Permanent naming rule

Phase and gate labels are planning metadata. They may appear in `TASK.md` and the handoff only; they must not become
source filenames, CTest names, C++ identifiers, macros, fixture directories, temporary-directory prefixes, JSON fields
or UI labels.

- [x] Rename `tests/Phase6Acceptance.cpp` to `tests/HistoricalBacktestAcceptance.cpp`.
- [x] Rename the CTest suffix to `HistoricalBacktestAcceptance`, the fixture macro to
  `DIDRACHMA_HISTORICAL_BACKTEST_FIXTURE_PATH`, the fixture directory to `tests/fixtures/historical-backtest/`, and
  internal test/function/temporary-directory names to equivalent domain language.
- [x] Replace the older `didrachma-editor-gate6a` temporary prefix with `didrachma-strategy-editor`.
- [x] Verify that a case-insensitive search for `phase6`, `phase-6`, `gate6` and `gate-6` outside `TASK.md` returns no
  source, test, fixture, CMake, JSON or UI matches.

### Full-range repeated strategy occurrences

- [x] Make a historical backtest process the complete inclusive range after target, stop-loss or condition exits.
  Allow at most one active position. After an exit within a bar, that bar's close may arm the next entry, but a
  next-bar-open order can fill only on a later bar. Do not skip or process a primary bar twice.
- [x] Represent the result as a chronological plural collection of strategy occurrences. Each occurrence needs a
  stable index/id, its own status, entry/exit values, P/L, events, evaluations and projection. Do not retain a singular
  `BacktestOutcome::result` as the hidden source of truth or silently expose only the first occurrence.
- [x] Preserve an outcome-level chronological audit/evidence stream across the complete requested range and associate
  every item with its occurrence when applicable. A range with no signal and a final signal that cannot fill must
  remain distinguishable without fabricating a trade. Store the association explicitly; do not rediscover it by
  comparing event kind, timestamp and detail strings.
- [x] Add an explicit summary containing reviewed, meaningful aggregates such as signal/filled/closed counts,
  target/stop/end-of-range counts and additive gross P/L, costs and net P/L. Keep per-occurrence return values; do not
  invent an aggregate percentage return without a defined capital model.
- [x] Update CLI JSON, Studio historical results and canonical Studio/CLI comparison to the plural model. Because the
  report shape changes from one result to a collection, bump the report format version and test null/absent fields.
- [x] In Strategy Monitor, show every filled occurrence chronologically with its own checkbox. Clicking an occurrence
  or one of its evidence rows must select it, navigate the normal chart to the relevant time and automatically enable
  that occurrence's overlay. It must remain rendered through zoom, pan and history loading until its checkbox is
  cleared. Clearing one occurrence must not hide the others.
- [x] Key chart overlays by historical-backtest id plus occurrence id/index and series key. Render the existing
  translucent profitable/loss position span, entry, stop, target and exit annotations for every enabled occurrence.
  Never merge several occurrences into one overlay or overwrite an earlier occurrence's span/profitability.
- [x] Remove `Focus strategy` from the chart context menu. Remove its stale selection/extent API and focused tests as
  well; occurrence-row/evidence navigation is the one supported way to jump to a strategy occurrence.
- [x] Extend normal chart hover without replacing the existing OHLCV values. When the hovered timestamp lies inside
  an enabled occurrence's entry/exit range, append strategy name, occurrence number/id, direction, entry and exit
  time/price, exit reason, P/L, and the stop/target values active at that timestamp. If enabled occurrences from
  different backtests overlap, show every matching occurrence in deterministic order. Disabled occurrences must not
  appear in hover information.
- [x] Keep hover lookup and formatting testable outside ImGui: query enabled overlays/occurrences by chart id and
  timestamp in Studio core, then let `apps/studio` only render the returned values.
- [x] Add a deterministic offline regression with one request containing at least three chronological filled
  occurrences and more bars after the first exit. Assert exact entry/exit times and prices, ordered exit reasons,
  non-overlap, complete-range evidence, per-occurrence projections and aggregate counts/P&L.
- [x] Run that repeated-occurrence request through the real runner, CLI application path and Studio historical
  orchestrator and compare their complete canonical semantic output, excluding only documented presentation metadata
  and measured wall-clock timings.

### Default strategy workspace layout

- [x] When Studio starts without an existing ImGui layout/configuration, dock `Strategies`, `Strategy editor` and
  `Backtest Run Setup` in the right-hand dock area, and dock `Strategy monitor` plus `Historical backtests` in the
  right-hand result/event area. They may share those areas as tabs with Profiles and Analysis Events.
- [x] Give these windows a usable first-run dock size. They must never first appear as tiny floating windows at the
  mouse cursor. Preserve an existing user layout on later starts; default docking is applied only when no layout exists.

### Strategy Monitor evidence presentation

- [x] Remove the complete `Attached strategy overlays` section from Strategy Monitor. Keep
  `HistoricalOverlayAttachment` as internal state for occurrence enable/disable, chart ownership and cleanup; do not
  replace it with a second user-facing list.
- [x] Give every occurrence-owned evaluation and audit event an explicit occurrence id in the historical result/timeline
  model. Preserve deterministic chronological ordering. Items that genuinely belong to the overall run rather than an
  occurrence must remain explicitly unassigned and appear under a clearly labelled run-level block.
- [x] Replace the dense pipe-separated evidence presentation with collapsible/readable blocks per occurrence. Use the
  same visible reference as the occurrence list, for example `Occurrence #1 (id 0)`, and include its entry/exit summary
  in the block header. Identify the `Entry armed` evidence explicitly as the entry trigger and show the exit trigger or
  exit reason separately, so the user does not have to infer them from raw events. Inside each block show
  timestamp/type/condition first, then truth/value/detail and finally binding/source/timeframe/source-bar/sequence
  metadata on separate wrapped lines.
- [x] Do not make every information row selectable. Show an explicit `Go to chart` action only when the item has a valid
  source key and timestamp. That action must select and enable the associated occurrence overlay before navigating.
  Non-navigable evidence remains readable plain information without a dead/no-op selection affordance.
- [x] Add deterministic Studio-core tests for explicit occurrence association, same-looking events in different
  occurrences, chronological ordering, unassigned run-level items and navigability. Keep ImGui limited to rendering the
  prepared presentation model.

### Deterministic acceptance matrix

- [x] Add one clearly named offline historical-backtest acceptance path that drives real `BacktestRequest` values
  through the real `BacktestRunner`. Do not construct finished results by hand to claim scenario coverage.
- [x] Cover and distinguish at least these outcomes:
  - `NoSignal`;
  - `SignalNotFilled`;
  - `Exited` through target;
  - `Exited` through stop-loss;
  - `OpenPositionClosedAtEnd` through end-of-range close.
- [x] For every scenario assert the exact result status, entry status, exit reason, recorded entry/exit time and price
  when present, ambiguity flag, costs, gross/net P/L, return and projection/audit data. Absent trade values must remain
  absent rather than becoming fabricated zeroes.
- [x] Include a deterministic strategy using a subject series and a fixed peer series on different timeframes. Its entry
  must contain nested boolean groups plus an ordered sequence. Assert the resolved source key/timeframe, condition path,
  truth, sequence progress and deterministic same-timestamp evidence ordering.
- [x] Keep all default acceptance fixtures offline, versioned text files under the existing strategy test/example
  structure. Do not use Yahoo, the current date, sleeps, ImGui, OpenGL, Xvfb or screenshots in default CTest.

### Studio and CLI equivalence

- [x] Feed the same immutable request and deterministic provider data through the Studio historical-backtest
  orchestration and `Didrachma_apps_strategy`. Compare canonical semantic output: request snapshot, resolved inputs,
  statuses, errors, result, audit evidence and projections.
- [x] Do not duplicate CLI argument parsing or backtest logic in Studio and do not weaken the comparison to a few labels.
  Presentation-only metadata such as a source filename and measured wall-clock timings may be excluded explicitly.
- [x] Supply normal importable strategy examples when the owner workflow needs them. The owner must not have to type
  internal ids or manually edit JSON to execute the final Studio workflow.

### Performance observability

- [x] Report these stages separately for a completed historical backtest:
  - data preparation: planning, provider history loading, derivation and input readiness;
  - indicator calculation: analysis calculations needed by the resolved strategy;
  - strategy execution: processing the in-range bars and finalizing the engine;
  - total runner duration.
- [x] Keep timing provider-neutral and use a monotonic clock. Timing is observational metadata and must never change
  decisions, ordering or result values. Failed and cancelled runs should retain whatever completed-stage timings are
  truthfully available rather than inventing values.
- [x] Expose the same timing breakdown in the versioned CLI JSON report and the Studio historical-result view. Keep UI
  formatting outside strategy/core.
- [x] Add deterministic structural tests for timing propagation and report fields, but do not assert machine-speed
  thresholds or exact real-clock durations in default CTest.

### Verification

- [x] Build strategy/core, studio/core, `Didrachma_apps_strategy`, `Didrachma_apps_studio` and the legacy
  `Didrachma_apps_chart` target.
- [x] Run the focused historical-backtest acceptance/equivalence tests with `--output-on-failure`.
- [x] Run the complete deterministic offline CTest suite with `--output-on-failure` and report the exact total,
  failures and explicit skips.
- [x] Confirm that the Yahoo strategy smoke remains explicit opt-in and is absent from the default offline CTest run.
  Do not run it unless the owner explicitly enables it; a network failure must never contaminate this gate.
- [x] Record the exact configure/build/test commands, scenario matrix and remaining risks in the final report.
- [ ] **OWNER-ONLY Windows workflow:** using the real Studio UI, import or select the supplied example without editing
  JSON or typing internal ids; configure subject, dates, provider and execution values in Backtest Run Setup; run it;
  use a range that produces multiple positions; inspect every occurrence, chronological evidence, checkbox persistence,
  overlapping enabled/disabled overlays, translucent position spans, chart hover information, direct goto behavior and
  the separate performance timings. With no saved ImGui layout, also confirm that all strategy windows start docked at
  a usable size on the right and that the chart context menu no longer contains `Focus strategy`. Confirm that the
  duplicate attached-overlay list is absent, evidence is grouped and labelled by occurrence, and only actionable
  evidence offers `Go to chart`; then
  delete the historical result and confirm the strategy definition and normal chart remain. Codex must leave this
  unchecked. Agent-operated GUI runs, screenshots, generated PNG files and Xvfb do not satisfy it.
- [x] Stop for owner review. Do not begin database, broker, portfolio, option-pricing or risk-management work.

## Delivery rules

- Work only on historical-backtest acceptance.
- Mark a checkbox only when that exact behavior exists and its relevant tests pass.
- Leave the owner-only Windows workflow unchecked for the user.
- If blocked, leave the item unchecked and report the precise blocker instead of doing unrelated work.
- Keep files focused and follow `AGENTS.md` module, thread, render and test boundaries.
- Do not add screenshots, PNG/JPG/PDF/ZIP files, logs, build output or other generated/binary evidence.
- Do not create a commit, branch, push, pull request or PR description.
- Leave ordinary source/text changes uncommitted for WinMerge review.
- Do not cite commit hashes, branches or repository history in the handoff.
- Do not introduce planning labels such as phase or gate numbers into code, tests, CMake targets, fixtures, schemas or UI.
- Report changed source/text files, architectural decisions, exact commands, test results and remaining risks, then stop.
