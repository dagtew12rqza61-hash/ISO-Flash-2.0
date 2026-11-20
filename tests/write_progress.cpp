#include "write_progress.hpp"

#include <cstdint>
#include <iostream>
#include <limits>

int main() {
    iso_flash::WriteProgress progress(1000);
    if (!progress.valid() || progress.completed_bytes() != 0 ||
        progress.remaining_bytes() != 1000 || progress.complete()) {
        std::cerr << "Initial progress state is invalid\n";
        return 1;
    }

    if (!progress.update(250) || progress.completed_bytes() != 250 ||
        progress.remaining_bytes() != 750 || progress.complete()) {
        std::cerr << "Partial progress update failed\n";
        return 1;
    }

    if (progress.update(249) || progress.completed_bytes() != 250) {
        std::cerr << "Progress accepted a regression\n";
        return 1;
    }

    if (progress.update(1001) || progress.completed_bytes() != 250) {
        std::cerr << "Progress accepted an overrun\n";
        return 1;
    }

    if (!progress.update(1000) || !progress.complete() ||
        progress.remaining_bytes() != 0) {
        std::cerr << "Completion progress update failed\n";
        return 1;
    }

    iso_flash::WriteProgress empty(0);
    if (empty.valid() || empty.update(0) || empty.complete()) {
        std::cerr << "Zero-length job was treated as valid progress\n";
        return 1;
    }

    iso_flash::WriteProgress maximum(std::numeric_limits<std::uint64_t>::max());
    if (!maximum.valid() ||
        !maximum.update(std::numeric_limits<std::uint64_t>::max() - 1) ||
        maximum.remaining_bytes() != 1 ||
        !maximum.update(std::numeric_limits<std::uint64_t>::max()) ||
        !maximum.complete()) {
        std::cerr << "Large byte counters overflowed or failed\n";
        return 1;
    }

    return 0;
}
