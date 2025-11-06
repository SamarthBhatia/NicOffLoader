#ifndef NICLOADOFF_SIM_TYPES_HH
#define NICLOADOFF_SIM_TYPES_HH

#include <cstdint>

namespace nicloadoff {

using SimTime = double;
using Duration = double;
using EventId = std::uint64_t;
using TaskId = std::uint64_t;
using ResourceId = std::uint32_t;

enum class EventType {
    kTaskArrival,
    kTaskReady,
    kTaskStart,
    kTaskComplete,
    kTransferStart,
    kTransferComplete
};

struct EventMetadata {
    EventType type{EventType::kTaskArrival};
    EventId id{};
};

} // namespace nicloadoff

#endif // NICLOADOFF_SIM_TYPES_HH
