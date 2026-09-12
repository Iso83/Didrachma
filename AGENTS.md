# Didrachma agent instructions

## Scope and priorities

Read `TASK.md` before changing code. Work on the first incomplete phase only, unless the user explicitly selects another phase.

Preserve the existing `apps/chart` ImPlot/Yahoo Finance demo. It is a reference and regression target, not the foundation of the new Studio application. Do not clean it up, move it, or fold it into Studio as part of this task.

The priorities, in order, are:

1. Preserve working behavior.
2. Keep module boundaries and thread ownership clear.
3. Add deterministic CTest coverage.
4. Implement the smallest complete part of the active phase.
5. Optimize only after correctness is measurable.

## Phase workflow

- Inspect the worktree and the active phase before editing. Never discard or overwrite unrelated user changes.
- Complete only one phase per review cycle. Stop after its review gate and report the result.
- Mark a checkbox in `TASK.md` only when that exact deliverable exists and its relevant tests pass.
- Never mark future work, partially implemented work, or an unverified build as complete.
- If blocked, leave the item unchecked and add a short `Blocked:` or `Note:` line below it.
- At the end of a phase, report changed files, architectural decisions, configure/build commands, CTest results, and remaining risks.
- Do not start the next phase until the user has reviewed the current one.

## Protected code and submodules

Do not edit anything under `extern/`, including nested repositories, generated files, dependency scripts, or submodule contents. Do not update submodule commits or stage a changed submodule pointer.

Reading and linking against an external module is allowed. If a required or generally useful change belongs in a submodule:

1. Keep the Didrachma-side solution behind a local adapter when practical.
2. Create or update root-level `SUGGESTIONS.md`.
3. Record the target repository and path, current limitation, proposed API or behavior, reason it belongs upstream, compatibility impact, and suggested tests.
4. Leave the external change for user review; do not treat the related task as complete when the local implementation is still blocked.

The repository-local TA-Lib integration must not modify `extern/CppDependencies`. If a reusable dependency helper would belong there later, record that as a suggestion while keeping the reviewed version or commit pinned in Didrachma.

## Build and test discipline

- Keep CTest enabled and meaningful. A successful compile is not a substitute for tests.
- Use an existing configured build directory or a dedicated agent build directory. Do not delete or repurpose a user's build tree.
- Run the smallest relevant test set while iterating, then run the complete Didrachma CTest suite with `--output-on-failure` at the phase gate.
- Build `Didrachma_apps_chart` at every phase gate even when the active phase does not modify it.
- Tests must be deterministic and must not depend on the network, the current date, Yahoo Finance availability, or a desktop OpenGL context unless explicitly labeled as integration/render tests.
- Put network-backed provider tests behind an opt-in integration label or option. Use fixtures and a fake provider for default CTest runs.
- Keep market, analysis, document, profile, time-alignment, and invalidation tests free of ImGui and OpenGL.
- Render tests must use a hidden context when available and skip clearly when the environment cannot create one. Core correctness must still be covered without GL.
- Reproduce and test a bug before fixing it when practical.

## Architecture rules

Keep dependencies directed toward the domain modules:

- Provider adapters such as `market/yahooFinance` implement `market/core` contracts.
- `analysis/core` depends on `market/core`; `analysis/taLib` implements `analysis/core` contracts.
- `stockChart/core` depends on `market/core` and `analysis/core`.
- `stockChart/render` and `studio/core` may depend on `stockChart/core`, but not on each other.
- `apps/studio` composes `studio/core`, `stockChart/render`, provider adapters, and the UI/runtime dependencies.

Application and rendering code may depend on domain modules; domain modules must not depend on applications, ImGui, ImPlot, GLFW, or OpenGL.

Additional boundaries:

- `market/core` owns provider-neutral timeframes, bars, series, requests, update semantics, and revisions. It does not calculate visual indicators.
- `analysis/core` owns indicator definitions, configured instances, typed parameters, results, conditions, and analysis events. It does not draw.
- `analysis/taLib` is the only module that exposes TA-Lib details.
- `stockChart/core` owns each chart document, visible time range, layer configuration, navigation, selection, and reusable indicator profiles. It does not issue GL or ImGui calls.
- `stockChart/render` owns ScopeCanvas integration, render invalidation, cached geometry, shaders, and GPU resources.
- `studio/core` owns workspace/session orchestration and persistence without ImGui widgets.
- `apps/studio` owns the ImGui docking shell and panels. Keep `Main.cpp` and panel composition thin.

Do not reuse ImPlot internals in new market, analysis, or StockChart modules. The legacy demo may continue using them until a later cleanup task.

## Thread and render ownership

- ImGui, ScopeCanvas viewport mutation, and every OpenGL call belong to the UI/GL thread.
- Providers and analysis workers may publish immutable results or value updates only. They must not retain widget, canvas, draw-context, or GL pointers.
- Deliver provider updates through an explicit thread-safe queue. Drain and apply them at a defined Studio frame boundary.
- Distinguish an appended closed bar from replacement of a forming bar, history backfill, and complete reset.
- Every mutable series has a monotonically increasing revision and a dirty time range.
- Updating model or analysis state does not automatically mean rebuilding GPU data. A chart render is invalidated only for a relevant data range, viewport, size, style, visibility, or selection change.
- An update outside the visible time range must still update the model and analysis/event list. It should not rebuild the chart texture unless auto-follow or another visible dependency requires it.
- Preserve ScopeCanvas canvas textures between frames. Do not redraw expensive StockChart content merely because ImGui starts a new frame.
- Store timestamps in an exact UTC representation. Convert visible timestamps to coordinates relative to a local origin before uploading floats to the GPU.

## C++ organization and style

- Keep files focused and below 500 lines. Split them before they become dump locations.
- Add a blank line when code moves to another logical subject.
- In a class, order declarations as: fields, constructors/destructor, operators, direct properties, then operations. Keep definitions in the `.cpp` in the same order as the header.
- A direct property only reads or writes its own fields and does not call other operations.
- Prefer a small function body of up to three simple statements in the header when doing so does not expose private dependencies or implementation helpers.
- Keep implementation helpers at the top of the relevant `.cpp` in `Didrachma::<Module>::Intern`.
- When an internal helper is shared by multiple `.cpp` files, put its declaration in a private header under that module's `src/` tree and keep it in the module's `Intern` namespace.
- Do not put internal helpers in public `include/` trees.
- Keep public headers minimal. Do not use `using namespace` in a header.
- Prefer RAII and explicit ownership. Raw pointers may observe non-owned objects but must not hide ownership.
- Use strong types or scoped enums for IDs, timeframes, update kinds, layer kinds, and invalidation reasons.
- Avoid sentinel strings such as `"ERROR"` in new APIs. Return an explicit result/error type while retaining a compatibility adapter for the demo.
- Preserve UTC and closed/forming-bar semantics at API boundaries; do not infer them from array positions.
- Format touched code with the repository configuration and avoid unrelated formatting churn.

## Terminology

Use these terms consistently in new code and UI text:

- **Indicator**: a calculation that produces one or more time-aligned outputs.
- **Indicator instance**: one chart-local indicator plus its parameters, enabled state, and visual style.
- **Profile**: a named reusable bundle copied into a new StockChart; one profile may be the default.
- **Layer**: a visual result such as a line, band, histogram, marker, or time span.
- **Condition**: a deterministic rule evaluated from market or indicator inputs.
- **Analysis event**: a timestamped condition occurrence with evidence and optional end time.
- **Signal**: an analysis event intended to attract the user's attention; do not use it as a synonym for every indicator output.

Do not introduce the misspelling `identicator` in code or user-facing text.
