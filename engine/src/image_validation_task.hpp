#pragma once

#include "image_fd_validation.hpp"

#include <exception>
#include <string>
#include <utility>

namespace iso_flash {

struct ImageValidationTaskOutcome {
    bool completed = false;
    ImageFdValidationResult validation;
    std::string error;
};

template <typename Validator>
ImageValidationTaskOutcome run_image_validation_task(int fd, Validator&& validator) {
    ImageValidationTaskOutcome outcome;
    try {
        outcome.validation = std::forward<Validator>(validator)(fd);
        outcome.completed = true;
    } catch (const std::exception& exception) {
        outcome.error = exception.what();
    } catch (...) {
        outcome.error = "Image validation failed with an unknown exception";
    }
    return outcome;
}

}  // namespace iso_flash
