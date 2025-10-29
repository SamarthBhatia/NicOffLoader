#include "nicloadoff/event_queue.hh"

#include <cassert>
#include <iostream>

using nicloadoff::EventQueue;
using nicloadoff::ScheduledEvent;

int main() {
    EventQueue queue;
    assert(queue.empty());
    assert(queue.size() == 0);

    queue.push(ScheduledEvent{.timestamp = 5.0, .id = 1});
    queue.push(ScheduledEvent{.timestamp = 1.0, .id = 2});
    queue.push(ScheduledEvent{.timestamp = 3.0, .id = 3});

    assert(!queue.empty());
    assert(queue.size() == 3);

    [[maybe_unused]] auto first = queue.pop();
    assert(first.has_value());
    assert(first->id == 2);

    [[maybe_unused]] auto second = queue.pop();
    assert(second.has_value());
    assert(second->id == 3);

    [[maybe_unused]] auto third = queue.pop();
    assert(third.has_value());
    assert(third->id == 1);

    [[maybe_unused]] auto none = queue.pop();
    assert(!none.has_value());
    assert(queue.empty());
    assert(queue.size() == 0);

    std::cout << "event_queue_smoke_test passed\n";
    return 0;
}
