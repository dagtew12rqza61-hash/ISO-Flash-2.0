#pragma once

namespace iso_flash {

enum class WriteJobState {
    Queued,
    Validating,
    Writing,
    Cancelling,
    Completed,
    Failed,
    DeviceLost,
    Cancelled
};

constexpr bool is_terminal(WriteJobState state) noexcept {
    return state == WriteJobState::Completed ||
           state == WriteJobState::Failed ||
           state == WriteJobState::DeviceLost ||
           state == WriteJobState::Cancelled;
}

constexpr bool can_transition(WriteJobState from, WriteJobState to) noexcept {
    if (is_terminal(from) || from == to) {
        return false;
    }

    if (to == WriteJobState::Failed || to == WriteJobState::DeviceLost) {
        return true;
    }

    switch (from) {
    case WriteJobState::Queued:
        return to == WriteJobState::Validating || to == WriteJobState::Cancelled;
    case WriteJobState::Validating:
        return to == WriteJobState::Writing || to == WriteJobState::Cancelling;
    case WriteJobState::Writing:
        return to == WriteJobState::Cancelling || to == WriteJobState::Completed;
    case WriteJobState::Cancelling:
        return to == WriteJobState::Cancelled ||
               to == WriteJobState::Completed;
    case WriteJobState::Completed:
    case WriteJobState::Failed:
    case WriteJobState::DeviceLost:
    case WriteJobState::Cancelled:
        return false;
    }
    return false;
}

constexpr bool transition(WriteJobState& state, WriteJobState next) noexcept {
    if (!can_transition(state, next)) {
        return false;
    }
    state = next;
    return true;
}

constexpr bool request_cancel(WriteJobState& state) noexcept {
    if (state == WriteJobState::Queued) {
        return transition(state, WriteJobState::Cancelled);
    }
    if (state == WriteJobState::Validating || state == WriteJobState::Writing) {
        return transition(state, WriteJobState::Cancelling);
    }
    return false;
}

}  // namespace iso_flash
