#include "nicloadoff/event_queue.hh"

namespace nicloadoff {

void EventQueue::push(const ScheduledEvent& event) { queue_.push(event); }

std::optional<ScheduledEvent> EventQueue::pop() {
    if (queue_.empty()) {
        return std::nullopt;
    }
    auto top = queue_.top();
    queue_.pop();
    return top;
}

std::optional<ScheduledEvent> EventQueue::peek() const {
    if (queue_.empty()) {
        return std::nullopt;
    }
    return queue_.top();
}

bool EventQueue::empty() const noexcept { return queue_.empty(); }

std::size_t EventQueue::size() const noexcept { return queue_.size(); }

} // namespace nicloadoff
