# Repository Guidelines

## Project Structure & Module Organization
- `sim/` hosts the simulator engine (event loop, resources, shared utilities); expect C++20 sources under `sim/src/` and tests under `sim/tests/`.
- `policies/` contains the DSL parser/runtime plus stock policy implementations; co-locate policy examples under `policies/examples/`.
- `workloads/` tracks DAG specs, arrival models, and workload templates; `profiles/` stores hardware parameter files and documentation.
- `experiments/`, `plots/`, and `thesis/` capture configurations, analysis scripts, and writing assets respectively; update the per-folder `README.md` files when structure evolves.

## Workflow & Status Tracking
- Consult `status.md` before starting work to see active phase goals, Done/Next/Remaining bullets, and recent decisions.
- After every coding session, append succinct updates under the touched phase: note shipped changes, queue the next actionable task, and flag remaining blockers.
- Create new sub-bullets only when a phase expands; otherwise edit in place so the record stays current.
- When a discrete feature wraps up, surface it promptly so we can open a pull request against `main`.
- Whenever a new user-facing feature lands (metrics, policies, manifests, loaders), add or refresh the TUI surface so the interactive view stays in parity with the CLI.
- Raise a pull request as soon as a coherent feature or fix is ready; don’t sit on stacked local changes—prefer smaller, reviewable PRs tied to status updates.
- Examples added under `policies/examples/` should ship with a manifest and, where feasible, a CLI or CTest regression that exercises them end-to-end.

## Build, Test, and Development Commands
- `cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo` configures the project (once CMakeLists lands); rerun after dependency changes.
- `cmake --build build` compiles simulator libraries, policy modules, and tests.
- `ctest --test-dir build` executes the unit/integration suite; keep it green before pushing.
- `ninja -C build benchmarks` (planned) produces performance harnesses once available.

## Coding Style & Naming Conventions
- Target C++20 with 4-space indentation, K&R-style braces, RAII helpers, and `const` correctness; prefer `.cc`/`.hh` or `.cpp`/`.hpp` consistently (default: `.cc`/`.hh`).
- Namespaces follow `nicloadoff::module`; classes use `PascalCase`, methods/functions `snake_case`, constants `kCamelCase`, and enums `PascalCase`.
- Ensure zero/low allocation on hot paths; document invariants near complex scheduling logic.
- YAML/JSON schemas live beside their loaders; validate inputs and surface actionable error messages.

## Testing Guidelines
- Place simulator unit tests under `sim/tests/` (e.g., `event_queue_test.cc`) and policy tests under `policies/tests/`; name test binaries `*_test`.
- Cover success paths and corner cases (resource contention, overflow conditions, state snapshots) with GoogleTest or Catch2; add property-based tests for invariants.
- Run `ctest --output-on-failure` before pushes; enable sanitizers (ASAN, UBSAN, TSAN) in CI for new subsystems.

## Commit & Pull Request Guidelines
- Follow Conventional Commit prefixes (`feat:`, `fix:`, `refactor:`, `test:`, `docs:`, `ci:`) with concise imperative summaries.
- PR descriptions should call out motivation, functional/performance impact, and include `cmake --build build` + `ctest` outputs (and benchmark diffs once available).
- Link relevant issues, mention configuration touches (profiles/workloads), and attach figures or logs only when they clarify experimental changes.
