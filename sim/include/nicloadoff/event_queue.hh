#ifndef NICLOADOFF_EVENT_QUEUE_HH
#define NICLOADOFF_EVENT_QUEUE_HH

#include "nicloadoff/sim_types.hh"

#include <compare>
#include <cstddef>
#include <optional>
#include <queue>
#include <vector>

namespace nicloadoff {

struct ScheduledEvent {
    SimTime timestamp{};
    EventId id{};
    EventMetadata metadata{};

};

class EventQueue {
  public:
    EventQueue() = default;

    void push(const ScheduledEvent& event);
    [[nodiscard]] std::optional<ScheduledEvent> pop();
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

  private:
    struct Comparator {
        bool operator()(const ScheduledEvent& lhs, const ScheduledEvent& rhs) const {
            return lhs.timestamp > rhs.timestamp;
        }
    };

    std::priority_queue<ScheduledEvent, std::vector<ScheduledEvent>, Comparator> queue_;
};

} // namespace nicloadoff

#endif // NICLOADOFF_EVENT_QUEUE_HH
