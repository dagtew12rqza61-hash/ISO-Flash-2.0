#include "image_validation_task.hpp"

#include <stdexcept>
#include <string>

int main() {
    const auto standard = iso_flash::run_image_validation_task(
        7, [](int fd) -> iso_flash::ImageFdValidationResult {
            if (fd != 7) throw std::runtime_error("wrong descriptor");
            throw std::runtime_error("simulated validation exception");
        });
    if (standard.completed ||
        standard.error != "simulated validation exception") {
        return 1;
    }

    const auto unknown = iso_flash::run_image_validation_task(
        9, [](int) -> iso_flash::ImageFdValidationResult {
            throw 42;
        });
    if (unknown.completed ||
        unknown.error != "Image validation failed with an unknown exception") {
        return 1;
    }

    const auto success = iso_flash::run_image_validation_task(
        11, [](int fd) {
            iso_flash::ImageFdValidationResult result;
            result.valid = fd == 11;
            result.bytes_hashed = 3;
            result.sha256 = "known-digest";
            return result;
        });
    if (!success.completed || !success.validation.valid ||
        success.validation.bytes_hashed != 3 ||
        success.validation.sha256 != "known-digest" || !success.error.empty()) {
        return 1;
    }

    return 0;
}
