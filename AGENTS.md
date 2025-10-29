# Repository Guidelines

## Project Structure & Module Organization
- `src/` houses the parser state machine, public interface, demo harness, and SIMD helper (current C sources scheduled for C++ refactors).
- `tests/` stores unit suites such as `parser_tests.c`; place new `test_*` cases beside existing ones and convert to C++ when dependencies require it.
- `benchmarks/` holds latency/throughput drivers (`bench_parser.c`) for hot-path regressions.
- Build artifacts (`http-server`, `parser_tests`, `bench_parser`) land in the repo root; clear them with `make clean` before committing.

## Workflow & Status Tracking
- Consult `status.md` before starting work to see active phase goals, Done/Next/Remaining bullets, and recent decisions.
- After every coding session, append succinct updates under the touched phase: note shipped changes, queue the next actionable task, and flag remaining blockers.
- Create new sub-bullets only when a phase expands; otherwise edit in place so the history reflects the latest plan of record.

## Build, Test, and Development Commands
- `make` builds the demo server with `-O3 -Wall -Wextra -march=native` (switch to C++ compiler flags as sources migrate to `.cc`).
- `make test` compiles and runs the parser regression suite; keep it green.
- `make bench` emits `bench_parser` and executes the throughput check; record deltas when tuning hot paths.
- `make clean` removes binaries; use before packaging patches.

## Coding Style & Naming Conventions
- Target C++20 (transitioning from C); prefer 4-space indentation, K&R-style braces, and RAII helpers where practical.
- `http_parser_*` remains the public prefix; internal state constants stay in all caps (`STATE_*`, `FLAG_*`), while new C++ types favor `PascalCase` classes and `snake_case` methods.
- Favor explicit bounds checks and early returns to preserve zero-allocation guarantees.
- Assembly in `simd_scan.S` must guard architecture-specific paths and fall back gracefully.

## Testing Guidelines
- Extend `tests/parser_tests.c` with `test_*` cases for new request shapes, error branches, and limit handling.
- When adding callbacks or flags, assert success paths and failure codes (e.g., `HTTP_PARSER_HEADER_OVERFLOW`) to avoid silent regressions.
- Run `make test` after each change; add sanitizers (`ASAN`, `UBSAN`) or fuzz harnesses when touching parsing logic.

## Commit & Pull Request Guidelines
- Follow Conventional Commit prefixes (`feat:`, `fix:`, `refactor:`, `test:`) with concise, imperative summaries.
- Each PR should describe motivation, note functional/perf impacts, and attach `make test` output plus `make bench` metrics when they move.
- Reference related issues, note platform considerations (SIMD flags, sanitizer findings), and include screenshots only when tooling output adds clarity.
