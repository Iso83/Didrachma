# Didrachma Studio and StockChart roadmap

## Goal

Build `apps/studio` as a dockable multi-StockChart application on top of the market modules, the indicator/analysis layer, and ScopeCanvas' persistent canvas rendering.

The current `apps/chart` ImPlot/Yahoo Finance demo remains behaviorally preserved and buildable. Its legacy data and chart classes are now private implementation details below `apps/chart/src`; Studio is a parallel implementation and must not reuse those classes as its foundation.

The first useful vertical slice is:

- open one symbol in Studio;
- show candles and volume in a ScopeCanvas-backed panel;
- add a TA-Lib moving average or Bollinger Bands instance;
- change its parameters and color;
- disable and re-enable it without losing configuration;
- avoid rebuilding chart geometry when nothing relevant changed.

## Current-code findings

- `market/core` now contains provider-neutral UTC bars, timeframes, series updates, provider contracts, revisions, dirty ranges, and a bounded update queue.
- `market/providers/yahoo` implements the provider contract. Its post-refactor interval, timestamp, cache-key, and legacy-adapter behavior still require the Phase 2A regression gate below.
- `analysis/core` exposes Didrachma-owned indicator types; `analysis/adapters/talib` owns all TA-Lib details and the initial SMA/Bollinger calculations.
- The preserved `apps/chart` implementation still owns its legacy parallel-array `TickerData`, ImPlot Bollinger calculation, synchronous UI flow, and `"ERROR"` compatibility sentinel. Do not copy these choices into Studio.
- The post-refactor target names and complete CTest suite have not yet been recorded in `TASK.md`; the Phase 0–2 command logs below are historical results from before the final module-path cleanup.
- ScopeCanvas `Canvas` already preserves a framebuffer texture and `DrawContext` exposes `needsRender()`. The current `ViewportHandler::draw()` still visits all registered viewports, so Studio must invoke expensive chart rendering selectively from repository-local code instead of changing `extern/ScopeCanvas`.

## Non-goals

- Do not clean up or replace `apps/chart` in this roadmap.
- Do not add ScopeCanvas.Editor scripting, IntelliSense, or a custom indicator language yet. Define extension seams only.
- Do not implement order entry, broker integration, portfolio accounting, or backtesting.
- Do not add a historical SQL database yet.
- Do not modify `extern/`, external submodules, or their commit pointers during the remaining Studio phases; the user-reviewed dependency and ScopeCanvas pointer changes in the refactor form the new baseline.
- Do not depend on TA-Lib's announced streaming API until it exists in the pinned release used by Didrachma.

## Target module structure

| Path | Responsibility | Important dependencies |
| --- | --- | --- |
| `market/core` | UTC bars, generic timeframes, history/update contracts, series revisions, dirty ranges | Standard library only where practical |
| `market/providers/yahoo` | Yahoo history adapter and later polling/live capability | `market/core`, curl, JSON |
| `analysis/core` | Indicator metadata, instances, parameters, outputs, condition graph, analysis events | `market/core` |
| `analysis/adapters/talib` | TA-Lib lifecycle, metadata and batch-function adapter | `analysis/core`, TA-Lib |
| `stockChart/core` | Per-chart document, layers, visible range, selection/navigation, profiles | `market/core`, `analysis/core` |
| `stockChart/render` | ScopeCanvas draw context, chart coordinate mapping, shaders, CPU/GPU caches | `stockChart/core`, ScopeCanvas render |
| `studio/core` | Open-chart sessions, provider/analyzer orchestration, persistence | Domain modules only |
| `apps/studio` | GLFW/ImGui docking shell and panels | `studio/core`, `stockChart/render`, provider and UI dependencies |
| `apps/chart` | Existing ImPlot demo and app-private legacy compatibility code; regression target only | Yahoo provider, ImPlot/UI dependencies |

Use namespace families matching the modules, for example `Didrachma::Market::Providers::Yahoo`, `Didrachma::Analysis::Adapters::TaLib`, `Didrachma::StockChart::Core`, and `Didrachma::StockChart::Render`. Put shared private implementation helpers under the corresponding `Intern` namespace and `src/` tree.

## Data and update model

Introduce value types with equivalent responsibilities; exact names may be refined during Phase 1:

- `Timeframe`: quantity plus unit, able to represent 10 minutes, 1 hour, and 1 day.
- `SeriesKey`: provider/instrument/timeframe identity without UI state.
- `Bar`: UTC open time, optional close time or duration, OHLCV values, and closed/forming state.
- `HistoryRequest`: key plus a half-open UTC time range.
- `BarUpdate`: append closed bar, replace forming bar, backfill range, or reset series.
- `BarSeries`: ordered storage with explicit duplicate/out-of-order rules, revision, and dirty time range.
- `ProviderCapabilities`: history, polling, and/or streaming support.
- `MarketDataQueue`: thread-safe value delivery to the Studio/UI boundary.

Do not align multiple timeframes by vector index. Evaluate a slower timeframe from the last closed bar whose timestamp is not after the faster evaluation timestamp. Keep forming bars explicit so a provisional value can never silently become historical evidence.

## Indicator and chart model

An `IndicatorDefinition` describes stable identity, display name, inputs, typed parameters, outputs, warm-up/lookback, and supported visual defaults. An `IndicatorInstance` belongs to exactly one StockChart and contains:

- a unique instance ID and definition ID;
- enabled state separate from existence;
- typed parameter values;
- per-output visual style, including color;
- source timeframe/input mapping;
- last calculated input revision and result revision;
- error or insufficient-history state without destroying its configuration.

Use these initial layer kinds:

- `Line` for values such as SMA/EMA;
- `Band` for upper/lower bounds with optional fill, such as Bollinger Bands;
- `Histogram` for volume or oscillator values;
- `Marker` for a point event such as an MA50 cross;
- `TimeSpan` for a condition with start and end.

A named `Profile` is a reusable bundle of indicator instance templates and visual defaults. Applying a profile to a new chart creates independent chart-local instances; later profile edits must not silently rewrite already-open charts. Allow one default profile.

An `AnalysisEvent` contains its condition ID, instrument, evaluation timeframe, start timestamp, optional end timestamp, direction/severity, title, summary, and structured evidence values. Selecting an event must update chart navigation through `stockChart/core`; the UI panel must not manipulate render buffers directly.

## Render and invalidation model

ImGui remains immediate-mode for menus, docking, lists, and panels. The expensive StockChart content is rendered to a persistent ScopeCanvas `Canvas` texture and displayed with `ImGui::Image`.

Track invalidation by reason:

- data range or revision changed;
- viewport/time or price range changed;
- canvas size changed;
- layer visibility, parameters, or style changed;
- selection/analysis marker changed.

Convert invalidations into coalesced UI-thread render tasks. Keep at most the newest required task per chart/layer revision; a background provider or analyzer may request rendering, but it may never execute a render task or touch GL itself.

Each render layer records the model/result revision, visible-range signature, style revision, and GPU range it represents. Rebuild only stale layers and upload only changed buffer ranges where practical.

Provider updates are always applied to the model and analysis engine. If their dirty time range does not intersect the visible chart range, do not rebuild the chart texture unless auto-follow is enabled or a dependent visible output changes. Show lightweight UI state such as “newer data available” outside the cached texture when useful.

Do not upload Unix epoch timestamps directly as `float`. Choose a local UTC time origin per visible/rendered chunk and upload relative coordinates to preserve precision.

All OpenGL resource creation, update, drawing, and destruction occurs on the UI/GL thread. Background work may calculate immutable CPU results only.

## TA-Lib integration constraints

- Consume the reviewed, version-pinned TA-Lib helper from `extern/CppDependencies`; it must prefer the configured CMake package and retain the pinned fetch fallback. Shared linkage is the default, while `STATIC` and `SHARED` remain valid explicit script choices.
- Do not change dependency helpers or submodule pointers as incidental Studio work. Make reusable dependency changes in `CppDependencies` and update its Didrachma pointer only as an explicit, separately reviewed change.
- Wrap `TA_Initialize` and `TA_Shutdown` in one process-lifetime RAII service and respect their single-threaded lifecycle requirements.
- Use the stable batch API. Do not code against the unreleased streaming API.
- Isolate `ta_libc.h` and `ta_abstract.h` from public Didrachma headers.
- Use TA-Lib lookback and returned begin/count values to align outputs with bar timestamps correctly.
- Use the abstraction metadata to support the indicator catalog and settings UI, but expose Didrachma-owned definitions and parameter types to the rest of the application.
- Start with SMA and Bollinger Bands. Add an indicator only when it has a deterministic fixture test and a mapped visual output.
- For incoming updates, recalculate only the conservative affected tail based on lookback and numerical-stability needs. Fall back to full batch recalculation when correctness cannot be guaranteed; record and measure that fallback.

## Phase 0 — Baseline and guardrails

- [x] Confirm the root configuration and current `Didrachma_apps_chart` target build without changing `apps/chart`.
- [x] Record the exact baseline configure, build, and CTest commands below this phase.
- [x] Restore deterministic CTest coverage for the legacy `TickerData` reserve/append behavior; after the refactor this test lives below `apps/chart` with the legacy type.
- [x] Restore the Yahoo provider as a test target using a checked-in JSON fixture and parser seam; the default test must not call Yahoo.
- [x] Add a regression check or explicit review step proving no file below `apps/chart/` or `extern/` changed.
- [x] Add the new module directories to CMake only as empty/minimal compilable targets needed by the next phase; do not implement later-phase behavior.
- [x] Build `Didrachma_apps_chart` and all newly introduced targets.
- [x] Run the complete default CTest suite with `--output-on-failure`.
- [x] Update only verified Phase 0 checkboxes, record results, and stop for user review.

### Phase 0 notes (historical, before the module-path refactor)

- Configure: `cmake -S . -B build-phase0 -DDidrachma_BUILD_TESTS=ON`.
- Build: `cmake --build build-phase0 --target Didrachma_apps_chart Didrachma_market_core_TickerData Didrachma_market_core_MarketData Didrachma_market_core_BarSeries Didrachma_market_core_MarketDataQueue Didrachma_market_core_LegacyAdapter Didrachma_tests_common_FakeProvider Didrachma_market_yahooFinance_Client Didrachma_market_yahooFinance_Parser -j2`.
- Test: `ctest --test-dir build-phase0 --output-on-failure` (8/8 passed).
- Regression review: `git diff --name-only -- apps/chart extern` produced no paths.
- Every market implementation/public interface has a separately registered `cppcmake_test_add` test and uses CppCMake's `TestAssert.h` (`CPPTEST_ASSERT` and `CPPTEST_RUN`).
- Yahoo's default test parses `tests/fixtures/chart.json` through the private parser seam and exercises only the unsupported-timeframe provider path; it performs no network request.
- No additional Phase 1 module directories were required at that checkpoint. Yahoo was subsequently moved from `market/yahooFinance` to `market/providers/yahoo`.

## Phase 1 — Provider-neutral bars and live-update contract

- [x] Add `Timeframe`, `SeriesKey`, `Bar`, `HistoryRequest`, `BarUpdate`, and explicit UTC/closed-bar semantics to `market/core`.
- [x] Implement the initial `BarSeries` sorted storage, append/replace/backfill/reset rules, monotonically increasing revision, and dirty time range; formalize duplicate precedence in Phase 2A.
- [x] Define provider history and subscription/update interfaces without exposing curl, Yahoo, ImGui, ImPlot, or GL types.
- [x] Implement a bounded/thread-safe market update queue with documented overflow/coalescing behavior.
- [x] Add a deterministic fake provider able to emit history, forming-bar replacements, and closed-bar appends.
- [x] Add a compatibility adapter from the provider-neutral bars to the app-private legacy `TickerData` and preserve the daily chart flow.
- [x] Adapt Yahoo daily history loading behind the new history contract; restore or explicitly narrow the remaining legacy interval behavior in Phase 2A.
- [x] Test the initial out-of-order, duplicate-timestamp, forming-to-closed, backfill, reset, revision, and dirty-range behavior; add the missing identity and edge-case coverage in Phase 2A.
- [x] Build `Didrachma_apps_chart` and run the complete default CTest suite.
- [x] Update only verified Phase 1 checkboxes, record results, and stop for user review.

### Phase 1 review gate (historical, before the module-path refactor)

- Configure: `cmake -S . -B build-phase1 -DDidrachma_BUILD_TESTS=ON`
- Build: `cmake --build build-phase1 --target Didrachma_apps_chart Test_Didrachma_market_core Test_Didrachma_market_yahooFinance -j2`
- Test: `ctest --test-dir build-phase1 --output-on-failure` (2/2 passed).
- Architecture: exact UTC timestamps and forming/closed state are value semantics; providers publish value-only updates; the bounded queue drops the oldest update on overflow and coalesces a reset by removing older updates for the same series.
- Compatibility: `apps/chart` remains unchanged. The root target defines `GLFW_INCLUDE_NONE` to make its existing GLAD/GLFW include order portable on Linux.
- Remaining risk: Yahoo's provider-neutral adapter supports daily history only and remains network-backed; default CTest exercises its capabilities and deterministic unsupported-timeframe error path without making a request.

## Phase 2 — Indicator core and TA-Lib adapter

- [x] Add `analysis/core` definitions for indicator metadata, typed parameters, configured instances, output series, warm-up state, revisions, and calculation errors.
- [x] Consume the reviewed, pinned TA-Lib helper supplied by CppDependencies without leaking TA-Lib into the domain modules.
- [x] Add the process-lifetime TA-Lib RAII service and translate return codes into Didrachma errors.
- [x] Read TA-Lib function display metadata for the supported SMA and Bollinger functions and expose Didrachma-owned definitions; verify the complete mapped metadata in Phase 2A.
- [x] Implement SMA with correct lookback, output begin/count, and timestamp alignment.
- [x] Implement Bollinger Bands as three timestamp-aligned outputs; grouping upper/lower outputs into one `Band` layer and the optional middle `Line` belongs to Phase 3.
- [x] Implement the initial conservative tail recalculation and expose `None`, `Tail`, or `Full`; harden cache identity and multi-instance behavior in Phase 2A.
- [x] Test metadata mapping, invalid parameters, insufficient history, gaps, lookback alignment, repeated updates, and known SMA/Bollinger fixtures.
- [x] Verify `analysis/core` public headers contain no TA-Lib, ImPlot, ImGui, GLFW, or GL includes.
- [x] Build `Didrachma_apps_chart` and run the complete default CTest suite.
- [x] Update only verified Phase 2 checkboxes, record results, and stop for user review.

### Phase 2 review gate (historical, before the module-path refactor)

- Configure: `cmake -S . -B build-phase2 -DDidrachma_BUILD_TESTS=ON`.
- Build: `cmake --build build-phase2 --target Didrachma_apps_chart Didrachma_analysis_core_Indicator Didrachma_analysis_taLib_Analyzer -j2`.
- Test: `ctest --test-dir build-phase2 --output-on-failure` (10/10 passed).
- Boundary check: `rg '#include.*(ta_|implot|imgui|GLFW|glad|OpenGL)' analysis/core/include` produced no matches.
- TA-Lib was provided by the reviewed, pinned CppDependencies helper. The later dependency/submodule refresh is part of the user-reviewed refactor and must become the new baseline before Studio work.
- Initial calculations and configuration changes use full batch recalculation. Repeated input revisions are skipped, while append/forming-bar dirty ranges recalculate and merge a lookback-sized tail.

## Phase 2A — Post-refactor verification and corrections

- [x] Make the root TA-Lib linkage request match the supported `cppdependencies_talib` API: default to shared, accept explicit `STATIC` or `SHARED`, and reject conflicting or unsupported arguments.
- [ ] Make all new test includes portable (`/`, not MSVC-only `\`) and verify public headers compile without relying on accidental include order.
- [ ] Define and test deterministic duplicate-timestamp precedence in `Bars`; also cover 10-minute, 1-hour, and 1-day identity, mismatched keys, rejected forming appends, and reset queue coalescing.
- [ ] Preserve the legacy chart adapter's daily, weekly, and monthly requests, or explicitly narrow its public contract. Include the interval in Yahoo cache identity, validate date parsing, and preserve sub-day timestamps whenever a sub-day interval is accepted.
- [ ] Keep Yahoo's default tests fully offline while covering every supported interval mapping and parser timestamp/close-time behavior.
- [ ] Fix the analyzer no-op guard so a changed definition or parameter set recalculates even when the market input revision is unchanged.
- [ ] Define analyzer cache ownership for multiple indicator instances; alternating instance IDs must not corrupt results or force an undocumented single-instance usage model.
- [ ] Add deterministic tests for same-input parameter changes, alternating instances, appended closed bars, forming-bar replacement, insufficient/error revision behavior, and the full-recalculation fallback.
- [ ] Verify that the supported SMA and Bollinger catalog metadata exposed to Didrachma is complete and test the mapped parameter/output metadata, not only IDs and visual enums.
- [ ] Configure and build the refactored targets: `Didrachma_apps_chart`, `Didrachma_market_core`, `Didrachma_market_providers_yahoo`, `Didrachma_analysis_core`, and `Didrachma_analysis_adapters_talib`.
- [ ] Run the complete default CTest suite with `--output-on-failure`, repeat the banned-include boundary check, record the current commands/results, and stop for user review.

### Phase 2A review gate

- Configure: `cmake -S . -B build-phase2-review -DDidrachma_BUILD_TESTS=ON`.
- Build: `cmake --build build-phase2-review --target Didrachma_apps_chart Didrachma_market_core Didrachma_market_providers_yahoo Didrachma_analysis_core Didrachma_analysis_adapters_talib -j2`.
- Test: `ctest --test-dir build-phase2-review --output-on-failure`.
- Boundary check: `rg '#include.*(ta_|implot|imgui|GLFW|glad|OpenGL)' analysis/core/include market/core/include` must produce no matches.
- Dependency check: no content or pointer below `extern/` changes after the reviewed refactor baseline.

## Phase 3 — StockChart document and profiles

- [ ] Add `stockChart/core` with a chart ID, instrument, primary timeframe, visible UTC range, price-range mode, and auto-follow state.
- [ ] Add chart-owned layer and indicator-instance collections with stable IDs.
- [ ] Represent a Bollinger `Band` as one layer binding its upper and lower outputs, with the middle output available as an independently visible `Line`.
- [ ] Allow add, remove, enable, and disable operations; disabling must preserve parameters, style, cached results, and list position.
- [ ] Add per-output visual style with at least color, visibility, line width, and band fill opacity where applicable.
- [ ] Add typed commands/events for viewport navigation, selected timestamp/range, and selected analysis event.
- [ ] Add named profiles, one optional default profile, independent copy-on-apply semantics, validation, and versioned serialization.
- [ ] Keep versioned profile serialization in `stockChart/core`, but leave storage and filesystem-path selection to the later `studio/core` repository.
- [ ] Test per-chart isolation, enable/disable retention, removal, style revision, default profile selection, copy-on-apply behavior, and serialization round trips.
- [ ] Build `Didrachma_apps_chart` and run the complete default CTest suite.
- [ ] Update only verified Phase 3 checkboxes, record results, and stop for user review.

## Phase 4 — ScopeCanvas StockChart renderer and one-chart Studio slice

- [ ] Add `stockChart/render` with a ScopeCanvas `DrawContext`, repository-local canvas host, coordinate mapper, and render invalidation state.
- [ ] Add a coalescing render-task scheduler that executes only on the UI/GL thread and drops superseded chart/layer revisions safely.
- [ ] Add `studio/core` with the minimal one-chart session and provider/analyzer orchestration required by this slice; keep it free of ImGui, ScopeCanvas, and GL types.
- [ ] Add `apps/studio` with GLFW, ImGui docking enabled, and a thin application/frame lifecycle.
- [ ] Render one StockChart canvas into a persistent ScopeCanvas texture and display it in an ImGui dock window.
- [ ] Render price grid/axes, candlesticks, and volume from the visible range without traversing or uploading the full history on every frame.
- [ ] Render SMA `Line` and Bollinger `Band` outputs through the same layer abstraction.
- [ ] Handle panel resize, focus, pan, zoom, and hit testing without editing ScopeCanvas.
- [ ] Render only canvases whose viewport or draw context is dirty; do not call a global draw-all path every ImGui frame.
- [ ] Keep GL buffer/shader ownership in RAII objects destroyed on the GL thread.
- [ ] Add measurable counters for canvas renders, layer rebuilds, uploaded bytes/elements, and skipped frames.
- [ ] Test coordinate transforms, visible-range clipping, dirty-reason transitions, out-of-view updates, and cache revision decisions without GL.
- [ ] Add an optional hidden-context smoke test for GL resource creation and one render when supported.
- [ ] Demonstrate that multiple unchanged ImGui frames do not increase the StockChart canvas-render counter.
- [ ] Build `Didrachma_apps_chart`, build Studio, and run the complete default CTest suite.
- [ ] Update only verified Phase 4 checkboxes, record results, and stop for user review.

## Phase 5 — Docked multi-chart indicator UI

- [ ] Support multiple independently dockable StockChart windows with stable session/chart IDs.
- [ ] Add an indicator catalog panel scoped to the currently selected StockChart.
- [ ] Add an indicator-instance panel with add, remove, enable/disable, reorder, and selection operations.
- [ ] Generate typed parameter controls from indicator metadata and validate before applying changes.
- [ ] Add per-output color and relevant style controls; style changes must invalidate only the affected layer.
- [ ] Add profile create, rename, update, delete, apply, and “set as default” UI.
- [ ] Apply the default profile when a new StockChart is created, while keeping the chart's copies independent.
- [ ] Persist and restore profiles plus open-chart workspace state through a versioned `studio/core` repository.
- [ ] Test two charts using different settings for the same indicator and verify that changes never leak between them.
- [ ] Build `Didrachma_apps_chart`, build Studio, and run the complete default CTest suite.
- [ ] Update only verified Phase 5 checkboxes, record results, and stop for user review.

## Phase 6 — Conditions, event list, and chart navigation

- [ ] Add deterministic condition interfaces that consume named market/indicator inputs and produce analysis events.
- [ ] Implement a first MA50 price-cross condition with upward/downward direction and structured evidence.
- [ ] Support point events (`Marker`) and start/end events (`TimeSpan`) without treating indicator lines as signals.
- [ ] Add an analysis-event list panel filtered by active chart and condition.
- [ ] Show event timestamp, direction/severity, concise reason, and expandable evidence/details.
- [ ] Selecting an event must select its marker/span, center or reveal its UTC timestamp, and preserve a sensible visible duration.
- [ ] Navigation must request missing history through `studio/core` rather than blocking the UI or reading provider data directly.
- [ ] Test exact cross behavior, equality, warm-up, gaps, duplicate update suppression, start/end pairing, sorting, filtering, and event-to-navigation mapping.
- [ ] Build `Didrachma_apps_chart`, build Studio, and run the complete default CTest suite.
- [ ] Update only verified Phase 6 checkboxes, record results, and stop for user review.

## Phase 7 — Multi-timeframe analysis foundation

- [ ] Add explicit input bindings so one custom indicator or condition can depend on 10-minute, 1-hour, and 1-day series/indicator outputs.
- [ ] Implement deterministic timestamp alignment using the most recent closed slower-timeframe value at or before the evaluation timestamp.
- [ ] Add a tested resampler for deriving a slower timeframe from a lower timeframe, with explicit session/timezone policy; do not silently invent missing bars.
- [ ] Add an acyclic dependency graph for series, indicators, and conditions with cycle and missing-input diagnostics.
- [ ] Propagate dirty ranges through the graph so only affected downstream tails are recalculated.
- [ ] Implement one built-in multi-timeframe trend-interest condition as proof of the model, with evidence from every timeframe.
- [ ] Keep condition construction independent of ScopeCanvas.Editor; expose a future factory/schema seam only.
- [ ] Test boundary timestamps, forming slower bars, gaps, session transitions, out-of-order updates, cycle detection, and no-look-ahead behavior.
- [ ] Build `Didrachma_apps_chart`, build Studio, and run the complete default CTest suite.
- [ ] Update only verified Phase 7 checkboxes, record results, and stop for user review.

## Phase 8 — Real-time flow, performance, and shutdown hardening

- [ ] Connect provider capabilities to Studio sessions and keep history, polling, and future streaming transports behind one update contract.
- [ ] Add Yahoo polling only if its endpoint behavior and rate limits are acceptable; otherwise keep it history-only and demonstrate live flow with the fake provider.
- [ ] Move network work off the UI thread with cancellation, bounded queues, retry/backoff, and visible connection/error state.
- [ ] Coalesce repeated forming-bar updates without losing the final closed bar or required analysis transitions.
- [ ] Continue model and analysis updates for off-screen data while suppressing unnecessary GL work.
- [ ] Make auto-follow explicit per StockChart and show when newer data exists outside the current view.
- [ ] Add load tests for long history, many indicator instances, repeated forming updates, multiple charts, and event-list growth.
- [ ] Record CPU calculation time, render time, queue depth, layer rebuild count, and uploaded elements; set reviewed regression thresholds rather than arbitrary FPS claims.
- [ ] Verify clean cancellation and destruction order for providers, workers, TA-Lib, chart sessions, canvases, ImGui, and the GL context.
- [ ] Run a manual Studio smoke scenario covering open chart, profile apply, live updates, indicator toggle, event navigation, docking, close/reopen, and clean shutdown.
- [ ] Build `Didrachma_apps_chart`, build Studio, and run the complete default CTest suite.
- [ ] Update only verified Phase 8 checkboxes, record results, and stop for user review.

## Final acceptance

- [ ] `apps/chart` still builds and behaves as the preserved ImPlot/Yahoo demo.
- [ ] Studio supports multiple docked StockCharts with independent indicator instances and profiles.
- [ ] Indicator disable/enable preserves configuration and results.
- [ ] SMA, Bollinger Bands, and MA50-cross behavior are covered by deterministic tests.
- [ ] An analysis event can reveal and select its timestamp/range in the correct chart.
- [ ] The multi-timeframe proof uses 10-minute, 1-hour, and 1-day closed data without look-ahead.
- [ ] Unchanged frames do not rebuild StockChart canvas content.
- [ ] Out-of-view updates do not cause unnecessary GL work and still reach analysis/event state.
- [ ] The complete default CTest suite passes without network or desktop dependencies.
- [ ] `extern/` contents and submodule pointers are unchanged after the reviewed post-refactor baseline.
- [ ] Any proposed external improvements are documented in `SUGGESTIONS.md` for separate review.
