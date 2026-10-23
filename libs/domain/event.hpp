#pragma once

#include <chrono>
#include <cstddef>
#include <deque>

namespace opennvr {

using MonoTime = std::chrono::nanoseconds;

class ConfirmationGate {
public:
    ConfirmationGate(std::size_t required = 2, std::size_t window = 3,
                     MonoTime max_age = std::chrono::seconds(2));
    bool sample(MonoTime time, bool person);
    void reset();

private:
    std::size_t required_;
    std::size_t window_;
    MonoTime max_age_;
    std::deque<std::pair<MonoTime, bool>> samples_;
};

class EventWindow {
public:
    explicit EventWindow(MonoTime post_roll = std::chrono::seconds(20));
    // Returns true only for the transition from idle to recording.
    bool person_seen(MonoTime now);
    // Called on a timer as well as on frames, so a lost detector cannot leave an event open forever.
    bool expire(MonoTime now);
    void reset();
    bool active() const noexcept { return active_; }
    MonoTime deadline() const noexcept { return deadline_; }

private:
    MonoTime post_roll_;
    MonoTime deadline_{};
    bool active_ = false;
};

} // namespace opennvr
