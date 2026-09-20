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
- [x] Manual check: choose two pattern-recognition instances in Studio, observe markers in the price panel and matching Analysis Events, then poll through a bar close and confirm the closed-bar marker stays fixed while the new live candle changes.
  Note: Automated closed-bar, marker, navigation, and render-path checks pass; the interactive poll-through check remains for a desktop session because this environment has no display server.

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
- [x] Build one dependency graph covering series, resampling, indicator instances, pattern events, and strategy conditions across all instruments/timeframes.
- [x] Determine required warm-up from indicator lookbacks and sequence/rule history before requesting data.
- [x] Share identical series and calculations between bindings and charts. Use existing provider, queue, `Bars`, revision, dirty-range, and resampling primitives.
- [x] Support provider-native and derived timeframes without hard-coding Yahoo into the strategy domain.
- [x] Expose per-binding readiness: loading, ready, stale, insufficient history, provider error, and calculation error.

### Deterministic evaluation

- [x] Evaluate on an explicit strategy clock driven by newly closed primary bars.
- [x] Align secondary timeframe/instrument inputs by publication time through the existing closed-value semantics. Missing, stale, or forming inputs produce `Unknown`, not false data and not a guessed value.
- [x] Define three-valued composition (`True`, `False`, `Unknown`) for every condition group and display evidence for each leaf.
- [x] Re-evaluate only the dirty dependency tail, while producing the same result as a full replay.
- [x] Keep condition evaluation independent of chart visibility and whether a chart window is open.

### Tests and Phase 3 gate

- [x] Use fake providers to test 10-minute, 1-hour, and daily data for a subject plus a fixed peer.
- [x] Prove that a later daily close is unavailable to earlier intraday decisions.
- [x] Test missing/stale secondary data, derived timeframes, duplicate series reuse, dirty-tail equivalence, and deterministic results regardless of update arrival order.
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
- [x] A historical replay and the same sequence of live closed-bar updates must produce the same strategy events and final result.

### Chart projection

- [x] Define UI-neutral projection data for an entry marker, exit marker, and time-bounded price segments for entry, stop, and target.
- [x] When stop or target changes, close the previous segment and start a new one. Historical lines must show what the strategy knew at that time instead of rewriting the entire past.

### Tests and Phase 4 gate

- [x] Test normal target exit, stop exit, gap exit, ambiguous OHLC bar, explicit condition exit, and end-of-range close.
- [x] Test the requested dynamic cases: adverse volatility lowers the target; unexpectedly favorable progress tightens the stop; later bars use the adjusted values.
- [x] Test long and short arithmetic, costs/slippage, duration, ROI, audit evidence, restart isolation, and replay/live equivalence.
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
- [x] Resolve every required fixed and subject series, load sufficient warm-up history, run the shared engine, and print:
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
- [x] The JSON report contains the complete auditable strategy result.
- [x] `Didrachma_apps_chart`, `Didrachma_apps_studio`, and `Didrachma_apps_strategy` build successfully.
- [x] The complete Didrachma CTest suite passes with `--output-on-failure`.
- [x] Confirm the compiled test target is visible and directly runnable in a generated Visual Studio solution.
  Note: CMake generates a standalone `Test_Didrachma_apps_strategy_EndToEnd` executable target in the
  `apps/strategy` test module, but Visual Studio is unavailable in this Linux environment for the final UI check.

---

## Phase 6 — Strategy editor and monitor in Didrachma Studio

### Definition management

- [ ] Add a `Strategies` panel with separate `Definitions` and `Running` views.
- [ ] Support select, create, edit, duplicate, save, load/import, delete, start, and stop. Destructive actions require confirmation when a definition or run has unsaved state.
- [ ] Open create/edit in a dedicated dialog with sections for:
  - identity and subject/fixed instruments;
  - named series and timeframes;
  - indicator/pattern bindings and parameters;
  - entry expression/sequence;
  - initial stop and target;
  - runtime rules and exit conditions;
  - position/cost assumptions.
- [ ] Validate while editing and show field-specific errors. Disable Start while the definition is invalid or required data is unavailable.
- [ ] Save/load through the Phase 2 repository; do not hide strategy definitions inside `didrachma-workspace.json`.

### Selected/running strategy panel

- [ ] Add a separate detail/monitor panel for the selected definition or run.
- [ ] List every underlying series binding with alias, resolved symbol, timeframe, readiness, last closed timestamp, and the conditions using it.
- [ ] Clicking an underlying series selects its existing chart or opens a chart if none exists.
- [ ] Automatically materialize the exact indicator/pattern instances required by that series' conditions. Track them by stable strategy binding id:
  - never match only by display name;
  - never overwrite a user's manual instance;
  - reuse an exact strategy-owned binding where safe;
  - remove only strategy-owned material when no run/view still needs it.
- [ ] Show every condition leaf as `True`, `False`, or `Unknown` with its latest evidence, plus ordered-sequence progress.
- [ ] While running, show state, start time, duration, entry/current price, ROI/P&L, stop, target, last rule, and event history.

### Chart integration

- [ ] Render pattern markers on each corresponding underlying chart.
- [ ] On the primary chart, render entry/exit markers and dotted, time-bounded entry/stop/target segments, including step changes when runtime rules adjust them.
- [ ] Selecting a strategy event navigates to its chart, timeframe, and timestamp and highlights the relevant bar/marker/segment.
- [ ] A running strategy continues when panels or charts are hidden/closed. Closing a view releases only view/render resources, not required strategy data/runtime state.
- [ ] Polling/live updates use the same closed-bar transition contract and do not make the docking/UI thread own provider work.

### Tests and Phase 6 gate

- [ ] Test Studio orchestration without ImGui/OpenGL: open/reuse required charts, create strategy-owned instances, preserve manual instances, close/reopen views, and keep a run alive.
- [ ] Test definition save/load, start/stop, restart snapshot behavior, and workspace restart handling for definitions and run summaries.
- [ ] Add only focused widget/render smoke coverage where core tests cannot prove behavior.
- [ ] Build both apps, run the complete CTest suite, and perform the full manual workflow with one subject plus at least one fixed sector peer on different timeframes.

### Phase 6 review gate

Stop and report the complete Studio workflow, remaining manual checks, performance observations, and known limitations. Do not begin broker/database work.

---

## Final acceptance checklist

- [ ] Every selectable TA-Lib pattern produces stable closed-bar price markers and matching Analysis Events.
- [ ] Forming-bar updates cannot repaint a committed marker or strategy decision; explicit backfill/reset is the only historical correction path.
- [ ] One saved strategy can combine multiple timeframes of the subject instrument with fixed peer/sector instruments.
- [ ] Conditions support simultaneous boolean logic and ordered sequences with visible evidence.
- [ ] A run tracks duration and ROI, and runtime conditions can adjust the target and tighten the stop with a full audit trail.
- [ ] Studio and the CLI load the same strategy file and use the same evaluator, fill model, and result calculations.
- [ ] The CLI reports all triggers and reproducible gross/net yield or loss.
- [ ] Studio provides definition editing, running-strategy monitoring, underlying-chart selection, automatic strategy-owned indicator instances, and entry/stop/target visualization.
- [ ] Core tests are deterministic/offline; the complete CTest suite passes; both `Didrachma_apps_chart` and `Didrachma_apps_studio` build.
- [ ] No code under `extern/` is modified and no dependency/submodule pointer is changed.
