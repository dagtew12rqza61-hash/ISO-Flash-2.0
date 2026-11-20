#include "write_job_state.hpp"

#include <array>
#include <iostream>

int main() {
    using iso_flash::WriteJobState;

    WriteJobState state = WriteJobState::Queued;
    if (!iso_flash::request_cancel(state) || state != WriteJobState::Cancelled ||
        !iso_flash::is_terminal(state)) {
        std::cerr << "Queued cancellation did not finish immediately\n";
        return 1;
    }

    state = WriteJobState::Queued;
    if (!iso_flash::transition(state, WriteJobState::Validating) ||
        !iso_flash::request_cancel(state) || state != WriteJobState::Cancelling ||
        !iso_flash::transition(state, WriteJobState::Cancelled)) {
        std::cerr << "Validation cancellation flow is invalid\n";
        return 1;
    }

    state = WriteJobState::Queued;
    if (!iso_flash::transition(state, WriteJobState::Validating) ||
        !iso_flash::transition(state, WriteJobState::Writing) ||
        !iso_flash::request_cancel(state) || state != WriteJobState::Cancelling ||
        !iso_flash::transition(state, WriteJobState::Cancelled)) {
        std::cerr << "Writing cancellation flow is invalid\n";
        return 1;
    }

    state = WriteJobState::Queued;
    if (!iso_flash::transition(state, WriteJobState::Validating) ||
        !iso_flash::transition(state, WriteJobState::Writing) ||
        !iso_flash::transition(state, WriteJobState::Completed) ||
        !iso_flash::is_terminal(state)) {
        std::cerr << "Successful completion flow is invalid\n";
        return 1;
    }

    constexpr std::array terminal_states{
        WriteJobState::Completed, WriteJobState::Failed,
        WriteJobState::DeviceLost, WriteJobState::Cancelled
    };
    constexpr std::array all_states{
        WriteJobState::Queued, WriteJobState::Validating,
        WriteJobState::Writing, WriteJobState::Cancelling,
        WriteJobState::Completed, WriteJobState::Failed,
        WriteJobState::DeviceLost, WriteJobState::Cancelled
    };

    for (const auto terminal : terminal_states) {
        for (const auto next : all_states) {
            auto candidate = terminal;
            if (iso_flash::transition(candidate, next) || candidate != terminal) {
                std::cerr << "Terminal state accepted another transition\n";
                return 1;
            }
        }
    }

    state = WriteJobState::Writing;
    if (!iso_flash::transition(state, WriteJobState::DeviceLost) ||
        !iso_flash::is_terminal(state)) {
        std::cerr << "Device loss was not terminal\n";
        return 1;
    }

    state = WriteJobState::Queued;
    if (iso_flash::transition(state, WriteJobState::Completed) ||
        state != WriteJobState::Queued ||
        !iso_flash::request_cancel(state) ||
        state != WriteJobState::Cancelled) {
        std::cerr << "Invalid transition changed job state\n";
        return 1;
    }

    return 0;
}
