# TASK — TA-Lib pattern recognition and strategy execution

## Goal

Extend Didrachma from configurable chart studies into a deterministic analysis and strategy environment:

1. TA-Lib Pattern Recognition functions can be selected and produce stable annotations on the price chart and entries in Analysis Events.
2. A saved strategy can combine conditions across multiple instruments and timeframes, start a tracked run, manage a stop and target while the run is active, and close with an auditable result.
3. The same strategy definition can be used by Didrachma Studio and by a CLI11 backtest application.

Work on one phase per review cycle. Complete the phase gate, report the result, and wait for user review before starting the next phase.

## Current state verified in `Didrachma 190926`

- The TA-Lib catalog already exposes Pattern Recognition definitions as `Indicator::Capability::AnalysisEvent` and their integer outputs as `VisualKind::Marker`.
- Studio currently disables every `AnalysisEvent` catalog item and shows the tooltip `Analysis/event capability; not a continuous chart study`.
- `Apps::Studio::add_indicator()` creates line, histogram, and band layers but ignores marker outputs.
- `ConditionBindingRegistry` currently creates events only for price/reference crosses and band breakouts. It does not convert TA-Lib pattern outputs into events.
- `LayerKind::Marker` exists in `stockChart/core`, but `stockChart/render` has no marker geometry or marker rendering path.
- Market bars already distinguish `Forming` from `Closed`, and provider updates distinguish `AppendClosed`, `ReplaceForming`, `Backfill`, and `Reset`.
- `MultiTimeframe::align_closed()` already provides a no-look-ahead primitive, but there is no multi-instrument strategy runtime or persisted strategy definition.
- CLI11 is already available at the root project level, but there is no strategy/backtest executable.

Do not replace these foundations with parallel types. Extend them or add small, clearly owned modules.

## Terminology and required semantics

- **Pattern recognition**: a selected TA-Lib candlestick-pattern definition. It is configured as an indicator instance but produces discrete analysis events and marker layers, not a continuous study line.
- **Strategy definition**: a versioned, persisted description of inputs, indicators, entry conditions, runtime rules, stop/target policy, and exit conditions.
- **Strategy run**: the mutable execution state created from an immutable snapshot of a strategy definition for one subject instrument.
- **Subject instrument**: a runtime-bound symbol supplied when a strategy is started or backtested.
- **Fixed instrument**: a symbol stored in the strategy, for example an index, ETF, sector peer, or another share used as context.
- **Condition**: a deterministic predicate. Conditions can be grouped with `All`, `Any`, and `Not`; an ordered sequence is a distinct group with an optional maximum duration between its steps.
- **Entry**, **stop**, and **target**: use these terms in code and UI. In the first implementation, “provisional end” means the current target/exit price, which runtime rules may adjust.
- **Strategy event**: an auditable state change such as entry armed, entry filled, stop changed, target changed, exit triggered, or run stopped.

### Closed-bar contract

- Pattern recognition and strategy decisions are committed only from `Closed` bars.
- A `Forming` bar may update the visible candle and provisional continuous indicator values, but it must not publish, remove, or rewrite a committed pattern event or strategy decision.
- When the forming bar becomes closed, evaluate it once and commit its result. The next forming bar is a different timestamp and may continue moving.
- Only an explicit `Backfill` or `Reset` may revise historical committed results. Such revisions must be identifiable in revisions/audit data; they must not look like ordinary live updates.
- Cross-timeframe and cross-instrument evaluation may use only values whose `available_at` time is less than or equal to the evaluation time. No future-bar or final-value leakage is allowed.

## Architecture constraints

- Keep `apps/chart` unchanged as the legacy regression application.
- Do not modify `extern/` or submodule revisions.
- Market/provider code owns bars and revisions, not strategy decisions.
- `analysis/core` owns reusable indicator/condition/event concepts and closed-value alignment; TA-Lib-specific interpretation stays in `analysis/adapters/talib`.
- Introduce a UI-independent strategy domain (prefer a focused `strategy/core` module) depending only on provider-neutral market and analysis APIs. It must not depend on Studio, StockChart rendering, ImGui, GLFW, or OpenGL.
- Studio charts are views of strategy inputs. A strategy must address series by stable bindings, never by pointers to open chart windows.
- Studio and the CLI must call the same strategy evaluator/runtime. Do not create a second backtest-only implementation.
- Provider callbacks publish immutable updates through queues. Apply updates, indicator calculations, pattern extraction, and strategy state transitions at defined runtime boundaries.
- Reuse one loaded/subscribed series for identical provider/instrument/timeframe keys. Do not download or poll the same series once per chart or condition.
- Persist exact UTC timestamps and explicit closed/forming state. Never infer bar state from vector position.
- Keep default CTest deterministic, offline, and independent of the current date or an OpenGL desktop.

## Out of scope for this task

- Broker order routing, paper/live trade submission, portfolio allocation, options pricing, margin, tax, and account synchronization.
- A general-purpose scripting language or arbitrary C++ plug-ins for strategies.
- Parameter optimization, walk-forward optimization, or Monte Carlo analysis.
- Database persistence. Versioned files are required now so the repository interface can be replaced by a database later.
- Promising that a pattern or strategy is profitable. The implementation measures observed results only.

---

## Phase 1 — Selectable TA-Lib Pattern Recognition

### Domain and TA-Lib adapter

- [x] Keep all supported TA-Lib Pattern Recognition functions visible in the Indicator Catalog and make them selectable.
- [x] Preserve each pattern's TA-Lib parameters, defaults, configured instance name, enabled state, and profile round-trip.
- [x] Convert every non-zero pattern output on a closed bar into one deterministic analysis event:
  - positive value: `Direction::Upward`;
  - negative value: `Direction::Downward`;
  - a non-directional non-zero output: `Direction::Neutral`;
  - zero: no event.
- [x] Store the raw TA-Lib pattern value in event evidence and include the pattern definition id, configured instance id/name, parameters, chart id, series key, and bar timestamp.
- [x] Give an event a stable identity derived from chart, instance, series, and bar timestamp so repeated recalculation replaces rather than duplicates it.
- [x] Do not publish events for a forming bar. Closing that same timestamp may create one event; subsequent `ReplaceForming` updates for the next bar must leave it unchanged.
- [x] Recompute affected pattern events for `Backfill`/`Reset` and remove events that are no longer valid within the explicitly revised range.
- [x] Keep generic event extraction independent of English display names and independent of whether a marker layer is visible.

### Price-panel annotations

- [x] Materialize a `LayerKind::Marker` when a pattern instance is added. Do not create a separate indicator pane for it.
- [x] Add provider-neutral marker geometry and rendering to `stockChart/render`:
  - bullish marker below the candle low;
  - bearish marker above the candle high;
  - neutral marker at a deterministic candle-relative position;
  - collision offset when several patterns occur on the same bar;
  - clipping to the visible time range and price pane.
- [x] Use readable default colors/shapes, while keeping style state attached to the layer rather than hard-coded in the Studio widget.
- [x] Selecting an Analysis Event navigates to and visually selects the corresponding marker/bar using the existing navigation flow.
- [x] Enabling, disabling, renaming, changing parameters, removing, saving, and restoring a pattern instance updates both its markers and its Analysis Events without affecting unrelated instances.

### Tests and Phase 1 gate

- [x] Add TA-Lib fixtures for at least one bullish and one bearish pattern and for a pattern with an optional parameter.
- [x] Test that zero outputs are ignored and signed outputs produce the correct direction and evidence.
- [x] Test `Forming -> ReplaceForming -> Closed -> next Forming`: no provisional event, one committed event after close, and no repaint from the next forming bar.
- [x] Test targeted correction through `Backfill` and full replacement through `Reset`.
- [x] Test stable event ids, duplicate suppression, independent configured instances, and profile/workspace round-trip.
- [x] Test marker placement and clipping without requiring an OpenGL context; keep only the final rendering smoke test behind the existing render-test policy.
- [x] Build `Didrachma_apps_chart` and `Didrachma_apps_studio`, then run the complete Didrachma CTest suite with `--output-on-failure`.
- [ ] Manual check: choose two pattern-recognition instances in Studio and observe markers in the price panel with matching Analysis Events.
- [x] Manual Yahoo polling check: poll through `Forming -> Closed -> next Forming` and confirm the closed-bar marker stays fixed while the new live candle changes.

### Phase 1 review gate

Stop after Phase 1. Report the selected patterns, event mapping, closed-bar behavior, changed files, build commands, tests, manual checks still required, and any remaining risks. Do not start the strategy model until reviewed.

---

## Phase 2 — Strategy definition, validation, and file persistence

### Versioned model

- [x] Add a UI-independent strategy definition with a stable id, display name, version, description, direction (`Long`/`Short`), and one primary series binding.
- [x] Model named series bindings with:
  - provider id;
  - instrument selector: `Subject` or `Fixed(symbol)`;
  - timeframe;
  - optional maximum data age/staleness policy.
- [x] Model named indicator bindings using a series binding, indicator definition id, parameters, and stable binding id.
- [x] Model condition expressions as a typed tree, not free-form strings:
  - market comparisons;
  - indicator comparisons/crosses;
  - pattern occurrence;
  - elapsed run time and closed-bar count;
  - unrealized return/loss;
  - `All`, `Any`, `Not`, and ordered `Sequence` groups.
- [x] For `Sequence`, persist the ordered steps and optional maximum elapsed time/bar count. A single occurrence must not silently satisfy several steps.
- [x] Model the initial entry plan, stop-loss, target, explicit exit conditions, and ordered runtime rules. Each runtime rule consists of a condition, priority, and one or more typed actions such as `AdjustStop`, `AdjustTarget`, or `Exit`.
- [x] Represent prices as explicit policies (absolute price, percentage from entry, or indicator-derived value). Do not encode price meaning in a bare `double`.
- [x] Validate duplicate ids, missing bindings, dependency cycles, bad indicator parameters, invalid timeframes, missing subject/fixed symbols, impossible price policies, and invalid condition/action references before a definition can run.

### Save/load contract

- [x] Add a repository interface plus a versioned JSON file implementation. Keep this separate from chart profiles and workspace persistence.
- [x] Preserve all definition ids and binding ids in a save/load round-trip.
- [x] Return structured validation/load errors with a JSON path or model field where practical.
- [x] Reject unsupported future format versions without partially loading them.
- [x] A running strategy uses an immutable snapshot. Editing and saving a definition must not mutate an already-running instance without an explicit restart.

### Tests and Phase 2 gate

- [x] Test a strategy using a subject instrument on 10-minute and 1-hour bindings plus a fixed sector peer on daily bars.
- [x] Test nested boolean groups and an ordered sequence, including timeout and out-of-order occurrences.
- [x] Test every validation failure above and exact JSON round-trip of a representative strategy.
- [x] Run the relevant tests, build both applications, and run the complete CTest suite.

### Phase 2 review gate

Stop after the versioned model and persistence are complete. Provide one readable example strategy JSON and explain how subject and fixed instruments are resolved.

---

## Phase 3 — Multi-series strategy data and condition evaluation

### Data graph

- [x] Resolve a strategy definition plus optional subject symbol into concrete provider-neutral `Series::Key` values.
- [ ] Build one dependency graph covering series, resampling, indicator instances, pattern events, and strategy conditions across all instruments/timeframes.
- [ ] Determine required warm-up from indicator lookbacks and sequence/rule history before requesting data.
- [x] Share identical series and calculations between bindings and charts. Use existing provider, queue, `Bars`, revision, dirty-range, and resampling primitives.
- [ ] Support provider-native and derived timeframes without hard-coding Yahoo into the strategy domain.
- [x] Expose per-binding readiness: loading, ready, stale, insufficient history, provider error, and calculation error.

### Deterministic evaluation

- [x] Evaluate on an explicit strategy clock driven by newly closed primary bars.
- [ ] Align secondary timeframe/instrument inputs by publication time through the existing closed-value semantics. Missing, stale, or forming inputs produce `Unknown`, not false data and not a guessed value.
- [x] Define three-valued composition (`True`, `False`, `Unknown`) for every condition group and display evidence for each leaf.
- [ ] Re-evaluate only the dirty dependency tail, while producing the same result as a full replay.
- [x] Keep condition evaluation independent of chart visibility and whether a chart window is open.

### Tests and Phase 3 gate

- [x] Use fake providers to test 10-minute, 1-hour, and daily data for a subject plus a fixed peer.
- [x] Prove that a later daily close is unavailable to earlier intraday decisions.
- [ ] Test missing/stale secondary data, derived timeframes, duplicate series reuse, dirty-tail equivalence, and deterministic results regardless of update arrival order.
- [x] Run the complete build/CTest phase gate.

### Phase 3 review gate

Stop after cross-series conditions can be evaluated headlessly and return evidence, without yet simulating trades.

---

## Phase 4 — Strategy run state machine and backtest engine

### Runtime lifecycle

- [x] Implement an explicit state machine at minimum covering `WaitingForEntry`, `EntryArmed`, `Running`, `Exited`, `Stopped`, and `Error`.
- [x] Record every transition and adjustment as a strategy event containing evaluation time, effective time, triggering rule/condition ids, old/new values, and evidence.
- [x] Entry conditions may be simultaneous or ordered according to their saved expression. A completed entry decision on a closed bar becomes effective at the next executable primary bar open to avoid look-ahead.
- [x] While running, evaluate runtime rules on every newly closed primary bar and track elapsed UTC time, closed-bar count, entry price, current price, unrealized P/L, maximum favorable excursion, and maximum adverse excursion.
- [x] Permit runtime rules to lower/raise the current target and tighten the protective stop. For a long run, a normal stop adjustment may not move lower; for a short run it may not move higher. If a future override policy is added, it must be explicit and audited.
- [x] Exit on an explicit exit condition, target hit, stop hit, user stop, or end of test range. Store the exact reason.

### Fill and result rules

- [x] Define a deterministic fill model shared by CLI and Studio:
  - condition-based entry/exit acts at the next primary bar open;
  - gap through a stop fills at the opening price, otherwise at the stop;
  - gap beyond a target fills at the opening price, otherwise at the target;
  - if stop and target are both touched within one OHLC bar and order is unknowable, use a documented conservative rule and flag the trade as ambiguous.
- [x] Support quantity, optional starting capital, fixed/percentage transaction costs, and configurable slippage so net yield/loss is reproducible.
- [x] Produce a result containing gross and net P/L, return percentage, duration, entry/exit data, drawdown/excursion, trigger counts, adjustment history, and ambiguity/warning flags.
- [ ] A historical replay and the same sequence of live closed-bar updates must produce the same strategy events and final result.

### Chart projection

- [x] Define UI-neutral projection data for an entry marker, exit marker, and time-bounded price segments for entry, stop, and target.
- [x] When stop or target changes, close the previous segment and start a new one. Historical lines must show what the strategy knew at that time instead of rewriting the entire past.

### Tests and Phase 4 gate

- [x] Test normal target exit, stop exit, gap exit, ambiguous OHLC bar, explicit condition exit, and end-of-range close.
- [x] Test the requested dynamic cases: adverse volatility lowers the target; unexpectedly favorable progress tightens the stop; later bars use the adjusted values.
- [ ] Test long and short arithmetic, costs/slippage, duration, ROI, audit evidence, restart isolation, and replay/live equivalence.
- [x] Run the complete build/CTest phase gate.

### Phase 4 review gate

Stop after the headless engine produces a complete deterministic run and result.

---

## Phase 5 — CLI11 strategy test application

- [x] Add a focused command-line executable (suggested target/name: `Didrachma_apps_strategy`) using CLI11.
- [x] Required/conditional arguments:
  - `--strategy <file>`;
  - `--subject <symbol>` when the definition contains `Subject` bindings;
  - `--from <UTC>` and inclusive `--through <UTC>`;
  - provider selection/configuration without leaking Yahoo types into strategy core.
- [x] Add optional `--output <json>`, cost/slippage overrides, and a verbose event trace. Reject an override that would make the result non-reproducible without recording it in the report.
- [ ] Resolve every required fixed and subject series, load sufficient warm-up history, run the shared engine, and print:
  - resolved inputs/timeframes;
  - readiness/errors;
  - entry, stop/target adjustments, and exit triggers;
  - gross/net yield or loss, percentage return, duration, and warnings.
- [x] Return non-zero for invalid strategy/configuration, missing required data, or an engine error. “No entry occurred” is a valid completed result and must be reported distinctly.
- [x] Make the JSON report versioned and machine-readable for later database import.

### Tests and Phase 5 gate

- [x] Add an offline end-to-end CLI test using fixture/fake data and a saved strategy file.
- [x] Test subject substitution and a fixed peer in the same run, inclusive end-date behavior, no-entry result, invalid definition, and missing data.
- [x] Keep Yahoo/network execution opt-in and outside default CTest.
- [x] Run the complete build/CTest phase gate.

### Phase 5 review gate

Stop after showing the exact CLI command and its deterministic summary/JSON output for the fixture strategy.

### Phase 5 acceptance verification

- [x] `Didrachma_apps_strategy` builds and its `Main.cpp` is only a thin entry point.
- [x] `Test_Didrachma_apps_strategy_EndToEnd` is a normal compiled C++ executable target.
- [x] The CMake-script harness is removed without losing scenario coverage.
- [x] Default CTest is deterministic, offline, and independent of the current date.
- [x] Console output, typed JSON fields, exit-code classes, and diagnostics are tested.
- [x] Strict UTC parsing is tested.
- [x] Unique-series loading and actual-bar-count warm-up are tested.
- [ ] The JSON report contains the complete auditable strategy result.
- [x] `Didrachma_apps_chart`, `Didrachma_apps_studio`, and `Didrachma_apps_strategy` build successfully.
- [x] The complete Didrachma CTest suite passes with `--output-on-failure`.
- [ ] Confirm the compiled test target is visible and directly runnable in a generated Visual Studio solution.
  Note: CMake generates a standalone `Test_Didrachma_apps_strategy_EndToEnd` executable target in the
  `apps/strategy` test module, but Visual Studio is unavailable in this Linux environment for the final UI check.

### Phase 5 corrective review — acceptance reopened on 2026-09-22

The CLI executable exists and its fixture tests pass, but Phase 5 is not accepted as a trustworthy backtest yet.
Review of the implementation found these concrete semantic gaps:

- `Definition::entry.price` is persisted and validated but never read by `Engine`; every entry is filled at the next
  primary bar open. The current editor therefore exposes a setting that has no effect.
- The successful CLI fixture opens at 101 and reaches its 105 target inside the same bar. Its reported duration is zero
  seconds. This proves plumbing, but it is not a meaningful end-to-end strategy example.
- `EntryArmed` at the end of the selected range is reported as `no_entry`, so “no signal occurred” and “a signal occurred
  but no executable bar remained” are indistinguishable.
- Execution costs/quantity are not part of the versioned strategy definition or a shared versioned run request. Studio
  stores them separately while the CLI starts from its own defaults, so identical execution is not yet guaranteed.
- Studio does not call the Phase 5 loading/range workflow at all. Its strategy start path opens charts with the fixed
  demonstration range `1700000000..1700086400` instead of a user-selected backtest range.

Do not continue the broad Phase 6 UI work until gates 5R.1A through 5R.4C are complete. Work on exactly one gate per
review cycle, update only that gate's checkboxes, report changed files and test output, then stop for review. If blocked,
report the precise blocker instead of doing unrelated refactors.

The implementation of 5R.1A through 5R.3 exists, but the trust review below reopened Phase 3 and final Phase 5
acceptance. Completed checkboxes in those earlier corrective gates describe retained implementation; they do not override
the open 5R.4 gates.

**Current next task: Gate 5R.4C only.**

How to interpret the older open checkboxes while completing the trust review:

- the remaining Phase 1 marker/Analysis Events observation is a user manual UI check. It does not block the backend
  5R.4 gates, but it remains required before final acceptance;
- the reopened Phase 3 dependency, warm-up, derived-series, dirty-tail, and combined-test items are acceptance rollups
  for Gate 5R.4B. The closed/stale-alignment rollup is re-accepted only after 5R.4A and the final 5R.4C replay check;
- the reopened Phase 4 replay/live-equivalence items and Phase 5 complete-report items belong to Gate 5R.4C;
- the Visual Studio target check is manual Windows acceptance. Leave it open until it is actually observed;
- do not run these older sections as separate phases and do not check their rollups early. The authoritative execution
  order is Gate 5R.4B, then 5R.4C, then Phase 6.

#### Gate 5R.1A — Correct the persisted domain contract

- [x] Document and implement these separate concepts in UI-independent code:
  - `Strategy::Core::Definition`: reusable rules; no ticker chosen for a `Subject` binding and no backtest dates;
  - `BacktestRequest`: strategy snapshot/id, subject symbol, inclusive `from`/`through`, provider configuration,
    quantity, optional starting capital, costs, slippage, and fill model version;
  - resolved series: concrete provider + symbol + timeframe keys derived from definition plus request;
  - `BacktestOutcome`: request snapshot, input readiness/errors, audit events, and final result.
- [x] Replace the misleading `EntryPlan::price` contract. The minimum accepted entry order model is:
  - `NextBarOpen`: after the entry condition becomes true on a closed primary bar, fill at the next executable primary
    bar open;
  - `Limit`: a fixed limit price plus a positive validity expressed in primary bars.
- [x] Do not offer “percentage from entry” as an entry order: there is no entry price yet. Keep percentage-from-entry for
  stop, target, and runtime adjustments, calculated from the actual filled entry price.
- [x] Treat a desired entry price range as conditions, not as an order-price hack. Existing boolean conditions must be
  able to express `price >= minimum AND price <= maximum`; later Studio UI will expose this as a simple “Price range”
  preset.
- [x] Version the JSON contract for the corrected entry model. Load the old format only through an explicit migration
  that maps its ignored entry-price field to `NextBarOpen` and returns a visible warning; never silently pretend the old
  value was honored.
- [x] Add model/validation/repository tests for valid and invalid entry orders, valid and invalid backtest requests,
  JSON migration warning, and exact round-trip of the new contracts.
- [x] Run only the affected strategy/core model, validation, and repository tests; report the proposed JSON example and
  stop. Do not change runtime execution, CLI, or Studio in Gate 5R.1A.

#### Gate 5R.1B — Implement and prove entry execution

- [x] Make `Engine` consume the persisted entry order; no entry setting may remain validated but ignored.
- [x] Preserve next-open semantics for `NextBarOpen`.
- [x] Define and implement deterministic limit fills:
  - long: if the next bar opens at or below the limit, fill at the better open; otherwise fill at the limit only when the
    bar low reaches it;
  - short: if the next bar opens at or above the limit, fill at the better open; otherwise fill at the limit only when the
    bar high reaches it;
  - if not filled before validity expires, emit an audited `EntryExpired` event and return to waiting for a new signal;
  - use a documented conservative result/warning if entry plus stop/target could occur in one OHLC bar and event order
    is unknowable.
- [x] Distinguish a run with no signal from a final-bar signal or expired limit that never filled.
- [x] Add runtime tests proving next-open fill, long/short limit fill, favorable gap fill, expiry, final-bar signal without
  a fill, percentage stop/target from the actual fill, and replay/incremental equivalence.
- [x] Run strategy/core runtime tests and stop. Do not change CLI or Studio in Gate 5R.1B.

#### Gate 5R.2 — One provider-neutral backtest runner for CLI and Studio

- [x] Extract the loading, warm-up, readiness, date filtering, engine execution, and finish/report input currently owned
  by `apps/strategy/Application.cpp` into one UI-independent backtest runner. CLI and Studio must call this same runner;
  Studio must not recreate a second orchestration path.
- [x] Define the range contract precisely: evaluate primary bars whose close time is in inclusive `[from, through]`;
  data before `from` is warm-up only; data after `through` may not influence a decision or fill.
- [x] Return structured errors for invalid ranges, no bars in range, insufficient warm-up, unsupported timeframe,
  provider failure, unresolved subject, and calculation/engine failure.
- [x] Validate requested timeframes against provider capabilities before downloading. For Yahoo, only expose supported
  frames from `Market::Providers::Yahoo::intervals()`; `2000 Minute` must be rejected before creating a chart or run.
  Keep Yahoo-specific capability discovery outside strategy/core.
- [x] Separate orchestration status (`Preparing`, `Ready`, `Executing`, `Completed`, `Failed`, `Cancelled`) from the
  trading state (`WaitingForEntry`, `EntryArmed`, `Running`, `Exited`, and so on).
- [x] Ensure the result distinguishes at least: `NoSignal`, `SignalNotFilled`, `OpenPositionClosedAtEnd`, `Exited`, and
  `Failed`.
- [x] Add offline tests with several realistically spaced bars and a non-zero-duration trade. Assert exact entry/exit
  timestamps, fill prices, range boundaries, stop/target, costs, and report status.
- [x] Run all strategy/core and apps/strategy tests and stop. Do not change the Studio editor in Gate 5R.2.

#### Gate 5R.3 — Re-accept the CLI backtest application

- [x] Adapt the CLI into a thin argument/report adapter over the shared backtest runner.
- [x] Prove that CLI arguments produce the same `BacktestRequest` and byte-equivalent result data as a direct runner
  call, apart from presentation metadata.
- [x] Replace the zero-duration happy-path fixture with a readable subject + fixed peer example covering several bars.
- [x] Keep a distinct regression test showing an entry signal on the final selected bar does not become a fictitious
  fill and is not mislabeled as “no signal”.
- [x] Add an opt-in Yahoo smoke command for AAPL using a supported timeframe and valid historical range. Network access
  remains outside default CTest, but the exact command and expected input summary must be reported.
- [ ] Build `Didrachma_apps_strategy`, run its compiled CTest target and the complete offline CTest suite, show one full
  JSON report, and stop for review. Only after approval may Phase 6 resume.

### Phase 5 trust review — acceptance reopened on 2026-09-24

The realistic CLI fixture and shared runner are retained, but source review found correctness paths that the green tests
do not exercise:

- `DataGraph` currently converts TA-Lib output from a `Forming` bar into a `TimedValue` with `closed=true` and makes it
  available at the bar's open timestamp. A secondary forming indicator or pattern can therefore influence a closed
  primary-bar decision.
- `maximum_data_age` is enforced for direct market comparisons but not for indicator comparisons, crosses, or pattern
  occurrences.
- dirty detection and derived-series maintenance are incomplete; the tests do not prove high/low/open/volume-only
  corrections or continued derivation after source updates.
- warm-up/dependency calculation covers only part of the definition, uses parameter heuristics instead of the analyzer's
  effective lookback, and may report `Ready` when an analyzer returned `InsufficientHistory`.
- every positive fill-model version is accepted although only model 1 exists.
- the JSON report refers to a mutable strategy file but does not contain the immutable strategy snapshot or an
  equivalent content-addressed identity.

Fix these in the three gates below. Work test-first, preserve the accepted entry/fill behavior, and do not redesign the
strategy editor. At the end of each gate, update only that gate, report exact changed files and commands, and stop for
review.

#### Gate 5R.4A — Enforce closed and non-stale strategy inputs (completed and reviewed 2026-09-24)

Scope: `strategy/core` evaluation plus the smallest required analysis-core/adapter contract change. Do not change
Runtime fill behavior, CLI presentation, Studio/ImGui, Phase 6, or `extern/`.

Review of sandbox `Didrachma 240925 1447` accepted the forming indicator/pattern publication fix. The gate remains open
because an `IndicatorCross` is calculated from four age-sensitive samples: current left/right and previous left/right.
The implementation checks only the current samples. Missing previous samples also return `Unknown` without actionable
evidence, and the fresh-path test accepts any non-`Unknown` result instead of proving the expected crossing truth.

Review of sandbox `Didrachma finish Gate 5R.4A review corrections only` accepted the follow-up: all four cross operands
are checked at their respective strategy-clock times, missing/stale evidence identifies current/previous and left/right,
constant-right crosses remain supported, and exact fresh `True`/`False` paths are covered. The user confirmed CTest and
ALL_BUILD remain green. Gate 5R.4A is accepted; do not reopen it during 5R.4B.

- [x] Add a deterministic regression test with a closed primary bar and a secondary `Forming` bar whose continuous
  indicator output would otherwise make an entry condition true. The indicator leaf must remain `Unknown` until that
  exact secondary bar closes.
- [x] Add the equivalent regression for `PatternOccurrence`: a non-zero output on a secondary forming candle must not
  advance a sequence or arm an entry; closing it may publish exactly one usable occurrence.
- [x] Preserve the source bar's closed/forming state when materializing indicator values. A forming sample must never be
  passed to `align_closed()` as closed and must not use its open timestamp as publication time.
- [x] Apply the source `SeriesBinding::maximum_data_age` policy consistently to `IndicatorComparison`,
  `PatternOccurrence`, and every sample used by `IndicatorCross`: current left/right and previous left/right, evaluated
  against their respective strategy-clock times. Missing, stale, or forming inputs return `Unknown` with actionable
  evidence identifying the side and whether the current or previous sample failed.
- [x] Complete the freshness regressions. Retain the current direct-market, indicator, pattern, current-left, and
  current-right cases; add stale/missing previous-left and previous-right cross cases. For every fresh case assert the
  exact expected `True` or `False` result and source time, not merely `truth != Unknown`.
- [x] Run the affected analysis/core and strategy/core evaluation tests. Demonstrate that the new previous-sample tests
  fail against the pre-correction implementation and pass after the fix; report exact commands, changed files, and any
  compatibility decision, then stop for review. Do not start 5R.4B in the same pass.

#### Gate 5R.4B — Repair invalidation, derivation, warm-up and readiness (completed and reviewed 2026-09-25)

Do not start this gate until 5R.4A is reviewed. Do not change Runtime fills, CLI presentation, Studio/ImGui, Phase 6, or
`extern/`.

Gate 5R.4B was accepted on 2026-09-25 both substantively and through the required build/CTest verification.

- [x] Make same-count replacement detection compare every decision-relevant bar field: open/close timestamp, state,
  open, high, low, close, and volume. The dirty range begins at the changed source bar's `open_time`, because indicator
  samples are keyed by `open_time`; do not begin at `close_time` and accidentally skip a zero-lookback calculation.
  When `DataGraph::apply()` seeds a temporary `Bars` model from existing history, clear that seed/reset dirty range
  before applying the real update so an append or small backfill does not dirty all history.
- [x] Add separate regression cases for high-only, low-only, open-only, volume-only, close-time, state-only,
  `ReplaceForming`, `Backfill`, and `Reset` corrections. Compare the complete incremental evaluations and indicator
  outputs with a fresh full replay, including evidence/source timestamps, not only the final truth value.
- [x] Make derived-series relationships real dependency-graph edges. An indicator bound to a derived series must depend
  on the resampling result, which in turn depends on its source series; a disconnected `resampling:<id>` node is not
  sufficient. A source append, forming replacement, backfill, or reset must automatically refresh the affected derived
  bucket/tail after the initial `derive_series()` relationship is registered.
- [x] Propagate source dirty ranges through resampling and indicator lookback using source-bar `open_time` semantics.
  Pass each indicator its own affected dirty range and recompute only the required tail. Add instrumentation/assertions
  for the reported `RecalculationKind`, dirty begin/end, calculated input slice, and unchanged-prefix reuse; equal final
  values alone are insufficient.
- [x] Register entry, explicit exit, and runtime-rule condition trees in the dependency graph. Warm-up planning must
  include analyzer lookback plus the history needed to reconstruct sequences from all three areas, including both
  `maximum_closed_bars` and `maximum_elapsed` where present.
- [x] Use the analyzer-owned `required_history()` contract for warm-up planning; keep TA-Lib lookback discovery in the
  adapter and remove/avoid strategy-core parameter-name or integer-value guessing. Add a nontrivial TA-Lib lookback test
  so a coincidental integer parameter cannot satisfy the acceptance criterion.
- [x] Propagate analyzer `InsufficientHistory` to binding readiness and to the runner's structured
  `InsufficientWarmup` failure even when the initially planned bar count was met. Never report that binding as `Ready`
  or silently continue with `Unknown`; add an explicit runner regression.
- [x] Preserve one provider load for identical resolved series keys and one analyzer calculation for identical
  calculation identities (same resolved source key, definition, parameters, and relevant input revision), while still
  exposing results under every configured binding id. Add counters proving reuse and proving derived inputs update once.
- [x] Run the affected analysis/core, TA-Lib adapter, strategy evaluation, and backtest-runner tests. Report commands,
  changed files, load/calculation counts, and any remaining performance risk; then stop for review.

#### Gate 5R.4C — Re-accept runtime equivalence and the auditable CLI report

Do not start this gate until 5R.4B is reviewed. Do not change Studio/ImGui or Phase 6.

- [ ] Reject every unsupported `fill_model_version` before provider loading. Version 1 remains the only accepted version
  until another fill implementation exists; add request, runner, repository, CLI-exit, and diagnostic tests.
- [ ] Make the versioned JSON report self-contained by storing the exact immutable strategy snapshot used by the run, or
  a content-addressed equivalent that cannot be confused after the source file changes. Keep the readable source path as
  optional presentation metadata only.
- [ ] Compare full replay with incremental closed-bar processing field by field: state, entry status, exit reason,
  timestamps, prices, P/L, ROI, duration, excursions, warnings, trigger counts, ordered audit events including evidence,
  and projection data. Comparing only event count and P/L is insufficient.
- [ ] Add a regression in which forming updates occur between closed updates and prove they cannot add, remove, or rewrite
  committed decisions. Historical correction remains possible only through explicit `Backfill`/`Reset` and must be
  identifiable in revision/audit data.
- [ ] Re-run the realistic subject + fixed-peer CLI fixture and the direct-runner comparison. Show the complete report
  and prove it contains the immutable strategy identity plus all execution assumptions.
- [ ] Build `Didrachma_apps_chart`, `Didrachma_apps_studio`, and `Didrachma_apps_strategy`; run the complete offline CTest
  suite with `--output-on-failure`.
- [ ] On Windows, confirm `Test_Didrachma_apps_strategy_EndToEnd` is visible and directly runnable in the generated Visual
  Studio solution. Leave this item open if it was not actually observed.
- [ ] Update the reopened Phase 3, Phase 4, and Phase 5 checkboxes only when their exact contracts are now proven. Report
  remaining manual checks and stop for user approval. Do not begin Gate 6A in the same pass.

---

## Phase 6 — Strategy editor and monitor in Didrachma Studio

### Phase 6 review result — rejected on 2026-09-22

The current implementation is not accepted as a usable strategy editor. The panel exposes internal ids as free-text
fields, uses the indicator catalog only during validation, and provides add-only editing for most collections. A user
can create structurally invalid objects but cannot complete or correct them without knowing repository-internal ids or
editing JSON outside Studio.

The checked items below have been reset to match what the implementation and manual review actually prove. Keep the
existing work where it is sound, but complete this corrective pass before checking an item again. Do not begin a later
phase and do not mark an item complete solely because a button, field, or unverified code path exists.

### Logical model the Studio must present

The UI must stop presenting storage ids as if they were trading concepts. Use this separation consistently:

| Concept | Meaning | Example shown to the user |
| --- | --- | --- |
| Strategy definition | Reusable logic, independent of one backtest period | “Daily + hourly trend confirmation” |
| Subject | The share supplied when a run starts | `AAPL` |
| Named series role | A readable role inside the strategy; it is not a ticker | “Subject 1 hour”, “Subject 15 minutes” |
| Fixed series | Context instrument stored in the definition | “Sector ETF — XLK — 1 day” |
| Backtest request | Concrete subject, from/through, provider and execution assumptions | `AAPL`, 2025-01-01 through 2025-12-31 |
| Run/result | One immutable request plus its loading state, events and outcome | “AAPL 2025 — completed — +7.2%” |

For a `Subject` series, the editor shows a role such as “Subject 1 hour”; it must not invite the user to type `AAPL` into
an alias and then separately type another symbol in a distant `Subject` field. The symbol is entered once in Run Setup.
A fixed comparison instrument shows its actual ticker directly in the series editor.

Backtest dates belong to Run Setup, not to the reusable strategy definition. Opening a run must never fall back to the
demo range in `Main.cpp`. Every loaded chart, calculation and report for that run uses the request's `from`/`through`
plus explicitly reported warm-up history.

### Phase 6 execution protocol

Complete exactly one gate per review cycle. Do not attempt all remaining Phase 6 items in one long change. At the end of
each gate: update only its checkboxes, list changed files, show the exact tests run, state manual checks still open, and
stop. The broad acceptance lists later in this phase are reference requirements, not permission to skip ahead.

#### Gate 6A — Finish the reusable strategy-definition editor

- [ ] Build on the existing `StrategyEditor` draft instead of replacing it. Prove deep-copy isolation for every nested
  condition, exit and runtime rule.
- [ ] Replace visible internal ids with human labels. Generated ids remain stable but appear only as read-only advanced
  information.
- [ ] Series cards use these controls: role name, provider, `Subject` or `Fixed`, fixed symbol when applicable, and a
  supported timeframe selector. Do not expose free integer/unit entry for Yahoo.
- [ ] The primary-series selector shows role names and enough context to distinguish timeframe/instrument.
- [ ] Indicator/pattern selection comes from the catalog, populates defaults, constrains parameters to metadata bounds,
  and offers only compatible outputs in later conditions.
- [ ] Finish all condition editors and the price-range preset. Add/remove/reorder and dependency blocking must work for
  every repeatable item.
- [ ] Replace the old entry-price widget with the Gate 5R entry-order controls: `Next bar open` or `Limit`, including
  limit validity. Stop/target keep absolute, percentage-from-filled-entry, and compatible indicator-derived policies.
  Note: the obsolete `entry.price` widget was removed during the Gate 5R compile repair. Gate 6A must restore an
  **Entry** section backed by `EntryPlan::order`; do not omit entry controls from the completed editor.
- [ ] `Apply`, `Cancel`, `Save`, and `Save As` have distinct behavior. Validation errors appear beside repairable fields.
- [ ] Construct, save, reload and compare a complete strategy using editor operations in a headless test.
- [ ] Run only editor/repository/core tests, report screenshots/manual gaps, and stop.

#### Gate 6B — Add an explicit Backtest Run Setup workflow

- [ ] Selecting a valid definition enables `New backtest`; it must not immediately create a run or chart.
- [ ] Run Setup contains: strategy name/version, subject ticker when required, inclusive `From` and `Through`, provider,
  quantity, optional starting capital, fixed/percentage costs, slippage, and fill-model version.
- [ ] Use proper date/time controls or validated ISO UTC input with clear examples. Display the provider's effective
  maximum history range for each selected timeframe.
- [ ] Show a resolved-input preview before execution: role, actual symbol, provider, requested timeframe, native or
  derived source interval, requested evaluation range, warm-up begin, and validation status.
- [ ] The primary action is `Run backtest`. Keep historical backtest and polling/live execution as visibly different
  modes; only historical backtest is required in this gate.
- [ ] Reject empty subject, invalid date order, future range, unsupported timeframe, and provider reach violations before
  starting background work. The screenshot case `pff / 2000 Minute` must be impossible to submit.
- [ ] Persist recent run setup separately from the reusable definition, without hiding it inside the chart workspace.
- [ ] Test request construction and validation headlessly, then add one focused ImGui smoke/manual check and stop.

#### Gate 6C — Execute Studio backtests through the shared runner

- [ ] `Run backtest` submits the exact `BacktestRequest` from Gate 6B to the accepted shared runner from Gate 5R.2.
- [ ] Provider/history work remains off the UI thread. Display `Preparing`, per-series loading/readiness, `Executing`,
  `Completed`, `Failed`, or `Cancelled` without creating a false trading run when data loading fails.
- [ ] Use the request range for provider loads and charts. Remove the hard-coded demonstration range from the strategy
  path.
- [ ] A run with no bars or an unsupported/rejected provider request shows the structured cause and does not present
  zero-valued P/L as if a strategy executed.
- [ ] Prove Studio and CLI create equivalent requests and results for the same fixture strategy, subject, dates and costs.
- [ ] Perform one real manual AAPL backtest over a valid supported Yahoo range and stop with its resolved-input summary.

#### Gate 6D — Results, evidence and chart projection

- [ ] Present completed historical backtests separately from active polling/live runs.
- [ ] Result header shows subject, date range, status, entry/exit time and price, exit reason, gross/net P/L, return,
  duration, costs, ambiguity and warnings. Use `—` instead of misleading zeroes for values that do not exist.
- [ ] Show every condition/group result with evidence at the evaluated bar, including sequence progress.
- [ ] Each event records its source series. Navigation opens the actual source chart/timeframe/timestamp rather than
  always using the primary chart.
- [ ] Strategy-owned chart instances are keyed by resolved series and cleaned up at every terminal state without touching
  manual instances.
- [ ] Closing a chart really releases view/render resources; stored run data remains available for reopening projections.
- [ ] Add headless orchestration tests plus focused projection/navigation tests and stop.

#### Gate 6E — Final Phase 6 acceptance

- [ ] Run the full manual workflow below without typing internal ids or editing JSON.
- [ ] Run the complete offline CTest suite and build chart, Studio and strategy applications.
- [ ] Report performance for data preparation, indicator calculation and backtest execution separately; do not describe
  an unmeasured long UI stall as acceptable.
- [ ] Update the global Phase 6 and final acceptance checkboxes only after the corresponding manual checks are observed.
- [ ] Stop for user review; do not begin database, broker or risk-management work.

### Cross-gate editor requirements

- Stable ids remain part of the persisted model, but routine editing must not require the user to know or type them.
  Generate collision-free ids when an item is created. Show them as read-only/advanced metadata where useful.
- Use model-aware selectors instead of free-text foreign keys:
  - primary series: dropdown containing the strategy's named series;
  - indicator series: dropdown containing the strategy's named series;
  - indicator definition: searchable selector populated from the supplied Indicator Catalog, preferably grouped by the
    catalog group and showing the display name;
  - condition series/indicator/output references: filtered dropdowns containing only compatible existing bindings and
    outputs;
  - indicator-derived price policies: indicator-binding and compatible-output dropdowns.
- Selecting an indicator or pattern from the catalog must immediately copy its parameter defaults into the binding and
  render editors from its `ParameterDefinition` metadata, including display name, type, minimum, and maximum. Changing
  the selected definition must reconcile parameters deliberately; it must not retain unrelated parameter ids.
- Provide edit and remove operations for every repeatable collection: named series, indicator/pattern bindings,
  condition children, sequence steps, exits, runtime rules, and runtime actions. Ordered collections must also support
  move up/down. `Not` must remain limited to exactly one child.
- Dependency-aware removal is required. If an item is referenced, either block removal and list the references or offer
  an explicit cascading repair. Never silently leave dangling ids. Re-select or clearly invalidate the primary series
  when its binding is removed.
- Implement complete controls for every Phase 2 condition type, not only a `Kind` dropdown:
  - market field, comparison operator, series and value;
  - indicator comparison binding, output, comparison operator and value;
  - indicator cross left/right binding and outputs or constant, plus cross direction;
  - pattern binding and optional direction;
  - elapsed duration and comparison;
  - closed-bar count and comparison;
  - unrealized gain/loss, comparison and percentage;
  - `All`, `Any`, `Not`, and ordered `Sequence`, including both maximum elapsed time and maximum closed bars.
- Editing must use a draft copy. `Apply`/`Save` commits the draft, `Cancel` restores the original, and closing or changing
  selection with dirty edits asks whether to save, discard, or continue editing. Merely focusing a widget must not mark
  the definition dirty.
- New definitions use `Save As`; an imported/saved definition saves back to its existing path unless `Save As` is
  chosen. Import/load errors and save errors must be visible in the panel. Do not use one ambiguous `File` textbox as
  both import source and save destination.
- Put validation messages next to the affected control and retain a compact validation summary. The editor must make
  every reported error repairable through the UI.

### Definition management

- [x] Add a `Strategies` panel with separate `Definitions` and `Running` views.
- [ ] Support select, create, edit, duplicate, save, save-as, load/import, delete, start, and stop as complete workflows.
  Destructive actions and selection changes must respect dirty editor state.
- [ ] Open create/edit in a dedicated editor with draft/apply/cancel semantics and complete sections for:
  - identity and subject/fixed instruments;
  - named series and timeframes;
  - indicator/pattern bindings and parameters;
  - entry expression/sequence;
  - initial stop and target;
  - runtime rules and exit conditions;
  - position/cost assumptions.
- [ ] Replace raw reference-id entry with the catalog/model-aware selectors and generated stable ids specified above.
- [ ] Support edit, dependency-aware remove, and ordering for every repeatable collection; the editor must no longer be
  add-only.
- [ ] Validate while editing and show field-specific, actionable errors. Disable Start while the definition is invalid,
  the required subject is absent, series resolution failed, or prepared input data is not ready.
- [x] Save/load through the Phase 2 repository; do not hide strategy definitions inside `didrachma-workspace.json`.

### Selected/running strategy panel

- [ ] Add a separate detail/monitor panel that supports both a selected definition before start and a selected/restored
  run. Do not show the empty “select a definition or run” state when a definition is selected.
- [ ] List every underlying series binding with display alias, resolved symbol, timeframe, readiness, last closed
  timestamp, and the actual conditions using it.
- [x] Clicking an underlying series selects its existing chart or opens a chart if none exists.
- [ ] Automatically materialize the exact indicator/pattern instances required by that series' conditions. Track them
  by stable strategy binding id plus resolved series/chart identity, so two subjects running the same definition cannot
  accidentally share an instance on the wrong chart:
  - never match only by display name;
  - never overwrite a user's manual instance;
  - reuse an exact strategy-owned binding where safe;
  - remove only strategy-owned material when no run/view still needs it.
- [ ] Release strategy-owned instances when a run reaches any terminal state, not only after an explicit user stop.
- [ ] Show every entry, exit, and runtime-rule condition leaf as `True`, `False`, or `Unknown` with its latest evidence.
  Show group results and ordered-sequence progress rather than hardcoded per-series `Unknown` text.
- [ ] While running, show named state, start time, duration, entry/current price, realized or unrealized ROI/P&L as
  appropriate, stop, target, last triggered rule, and event history. Do not expose enum values as unexplained integers.

### Chart integration

- [ ] Render and manually verify pattern markers on each corresponding underlying chart for a strategy-selected pattern.
- [x] On the primary chart, render entry/exit markers and dotted, time-bounded entry/stop/target segments, including step changes when runtime rules adjust them.
- [ ] Selecting a strategy event navigates to the event's actual series/chart, timeframe, and timestamp and highlights the
  relevant bar/marker/segment; do not route every event to the primary chart.
- [ ] Move strategy-required bars/provider sessions out of `ChartView`. A running strategy continues when panels or
  charts are hidden/closed, while closing a chart really releases its view/render resources. `close_view()` must have
  real, tested behavior rather than being a no-op.
- [ ] Polling/live updates use the same closed-bar transition contract and do not make the docking/UI thread own provider
  work. Detect same-count `Backfill`/`Reset` revisions; comparing only the number of closed bars is insufficient.
- [ ] Do not rebuild the complete engine and replay all history for every ordinary newly closed bar. Use the shared
  incremental/dirty-tail runtime path, while proving equivalence with full replay.

### Tests and Phase 6 gate

- [ ] Extract UI-independent editor operations where practical and test catalog selection, default parameter population,
  generated unique ids, compatible reference/output choices, changing definitions, dependency-aware removal, ordering,
  draft cancel/apply, and dirty-state transitions.
- [ ] Test every condition editor variant by constructing a valid strategy exclusively through editor operations and
  round-tripping it through the Phase 2 repository.
- [ ] Test Studio orchestration without ImGui/OpenGL: open/reuse required charts, create strategy-owned instances,
  preserve manual instances, run the same definition for two different subjects, clean up every terminal state,
  genuinely close/reopen views, and keep the strategy runtime alive independently of those views.
- [ ] Test definition save/load/save-as/import failures, start/stop, immutable running snapshots, and restart handling for
  saved definitions and historical run summaries.
- [ ] Test append-close, forming replacement, and same-count backfill/reset updates. Prove incremental results equal a
  full replay and that committed decisions are revised only for explicit historical corrections.
- [ ] Add focused widget/render smoke coverage for the selector and remove flows that core tests cannot prove.
- [ ] Build both apps, run the complete CTest suite, and perform the full manual workflow with one subject plus at least one fixed sector peer on different timeframes.

### Required manual acceptance workflow

- [ ] Create a new strategy without typing any internal id.
- [ ] Add subject series for 10-minute and 1-hour data plus a fixed daily sector peer; select the primary series from a
  dropdown, then remove and re-add a non-primary series.
- [ ] Add at least one continuous indicator and one TA-Lib pattern through the searchable catalog selector. Confirm that
  defaults and bounded parameter controls appear immediately, then change the selected definition and verify its
  parameters/outputs are reconciled.
- [ ] Build a nested entry expression and an ordered sequence using selectors only. Remove and reorder children, and
  configure both sequence timeout forms.
- [ ] Add and remove an exit, runtime rule, and runtime action. Verify referenced objects cannot be deleted silently.
- [ ] Select `Next bar open` entry and percentage-from-entry stop/target. Verify the editor does not request a fictitious
  absolute entry price. Then switch to a fixed limit order and configure its validity.
- [ ] Use the “Price range” entry-condition preset and verify it creates the intended lower/upper market comparisons.
- [ ] Cancel edits and prove the stored definition did not change; edit again, save, close Studio, reopen it, and load the
  same complete definition.
- [ ] Choose `New backtest`, enter subject `AAPL`, select an explicit inclusive `From` and `Through`, and review the
  resolved series before execution. Confirm that `AAPL` is the actual subject symbol and not a series alias.
- [ ] Verify only provider-supported timeframe choices are offered. Confirm arbitrary `2000 Minute` input cannot be
  created or submitted.
- [ ] Run the backtest only after request validation and data readiness. Confirm the result records the chosen dates,
  subject, effective warm-up, fill model and execution assumptions, and shows real leaf evidence/sequence progress.
- [ ] Repeat the exact request through `Didrachma_apps_strategy` and confirm Studio and CLI return the same entry, exit,
  P/L and status.
- [ ] Open every underlying chart, verify the strategy-owned indicators/patterns and primary entry/stop/target projection,
  close the chart view, and confirm the stored result remains available without retaining the view resource.
- [ ] Navigate an event originating on a secondary series and verify Studio opens/selects that exact chart and timestamp.
- [ ] Confirm `NoSignal`, `SignalNotFilled`, target/stop exit, and end-of-range close are visibly distinct. Verify only
  strategy-owned instances are released while manual instances remain.

### Phase 6 review gate

Stop and report the complete Studio workflow, remaining manual checks, performance observations, and known limitations. Do not begin broker/database work.

---

## Final acceptance checklist

- [ ] Every selectable TA-Lib pattern produces stable closed-bar price markers and matching Analysis Events.
- [ ] Forming-bar updates cannot repaint a committed marker or strategy decision; explicit backfill/reset is the only historical correction path.
- [ ] One saved strategy can combine multiple timeframes of the subject instrument with fixed peer/sector instruments.
- [ ] Conditions support simultaneous boolean logic and ordered sequences with visible evidence.
- [ ] Entry execution has audited next-open and fixed-limit semantics; no persisted editor option is ignored by the
  runtime, and percentage stop/target values use the actual fill price.
- [ ] A run tracks duration and ROI, and runtime conditions can adjust the target and tighten the stop with a full audit trail.
- [ ] Studio requires an explicit subject and inclusive backtest `from`/`through`, validates provider-supported
  timeframes/range limits, and never uses the demonstration chart range for a strategy run.
- [ ] Studio and the CLI load the same strategy file, construct the same backtest request, and use the same data-loading
  workflow, evaluator, fill model, and result calculations.
- [ ] The CLI reports all triggers and reproducible gross/net yield or loss.
- [ ] Studio provides definition editing, running-strategy monitoring, underlying-chart selection, automatic strategy-owned indicator instances, and entry/stop/target visualization.
- [ ] Core tests are deterministic/offline; the complete CTest suite passes; both `Didrachma_apps_chart` and `Didrachma_apps_studio` build.
- [ ] No code under `extern/` is modified and no dependency/submodule pointer is changed.
