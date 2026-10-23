#include "domain/event.hpp"

#include <algorithm>
#include <stdexcept>

namespace opennvr {

ConfirmationGate::ConfirmationGate(std::size_t required, std::size_t window,
                                 MonoTime max_age)
    : required_(required), window_(window), max_age_(max_age) {
    if (required == 0 || required > window || max_age <= MonoTime::zero()) {
        throw std::invalid_argument("Invalid confirmation window");
    }
}

bool ConfirmationGate::sample(MonoTime time, bool person) {
    if (!samples_.empty() && time < samples_.back().first) {
        throw std::invalid_argument("Confirmation clock moved backwards; reset the gate");
    }
    samples_.emplace_back(time, person);
    while (samples_.size() > window_ ||
           (!samples_.empty() && time - samples_.front().first > max_age_)) {
        samples_.pop_front();
    }
    // A negative current frame never refreshes post-roll using old positives.
    return person && static_cast<std::size_t>(std::count_if(
        samples_.begin(), samples_.end(), [](const auto& value) { return value.second; })) >= required_;
}

void ConfirmationGate::reset() { samples_.clear(); }

EventWindow::EventWindow(MonoTime post_roll) : post_roll_(post_roll) {
    if (post_roll <= MonoTime::zero()) {
        throw std::invalid_argument("Post-roll must be positive");
    }
}

bool EventWindow::person_seen(MonoTime now) {
    const bool started = !active_;
    active_ = true;
    deadline_ = std::max(deadline_, now + post_roll_);
    return started;
}

bool EventWindow::expire(MonoTime now) {
    if (!active_ || now < deadline_) return false;
    reset();
    return true;
}

void EventWindow::reset() {
    active_ = false;
    deadline_ = MonoTime::zero();
}

} // namespace opennvr
