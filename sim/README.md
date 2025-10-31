# sim/

Core simulator engine source lives here. Expect C++20 modules for the discrete-event loop, resource models, configuration loaders, and shared utilities.

## Layout

- `include/nicloadoff/event_queue.hh`: Priority queue wrapper that orders `ScheduledEvent`s by simulation time.
- `include/nicloadoff/sim_types.hh`: Canonical aliases for simulation time, identifiers, and event categories.
- `include/nicloadoff/profile.hh`: Data structures and loader interface for hardware profiles (minimal in-repo YAML parser).
- `src/`: Implementation files corresponding to the headers above.
- `tests/`: Smoke/unit tests built alongside the simulator library.

## Next Up

- Extend the profile loader with additional validation (negative fixtures, stricter type checks).
- Add richer unit tests for event sequencing and resource scheduling once core abstractions land.

_Last touched: 2025-10-31_
