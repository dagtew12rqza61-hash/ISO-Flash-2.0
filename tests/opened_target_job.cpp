#include "target_source.hpp"
#include "write_job.hpp"

#include <cerrno>
#include <fcntl.h>
#include <iostream>
#include <sys/sysmacros.h>
#include <unistd.h>

int main() {
    int image_pipe[2] = {-1, -1};
    int target_pipe[2] = {-1, -1};
    if (::pipe(image_pipe) != 0 || ::pipe(target_pipe) != 0) {
        std::cerr << "Cannot create descriptor fixtures\n";
        return 1;
    }
    ::close(image_pipe[1]);
    ::close(target_pipe[1]);

    iso_flash::OpenedImage image(
        iso_flash::UniqueFd(image_pipe[0]),
        iso_flash::ImageSnapshot{1, 2, 4096, 3, 4, 5, 6});
    iso_flash::OpenedTarget target(
        iso_flash::UniqueFd(target_pipe[0]),
        iso_flash::DeviceSnapshot{static_cast<std::uint64_t>(makedev(8, 16)),
                                  8U * 1024U * 1024U, false});

    if (!target.valid()) {
        std::cerr << "Synthetic admitted target is unexpectedly invalid\n";
        return 1;
    }
    const int image_fd = image.descriptor.get();
    const int target_fd = target.descriptor.get();
    const auto expected_target = target.snapshot;

    iso_flash::WriteJob job(77, std::move(image), std::move(target));
    if (!job.has_valid_resources() || !job.has_image_snapshot() ||
        !job.has_target_snapshot() || job.image_fd() != image_fd ||
        job.target_fd() != target_fd ||
        job.target_snapshot().device_number != expected_target.device_number ||
        job.target_snapshot().capacity_bytes != expected_target.capacity_bytes ||
        job.target_snapshot().read_only ||
        image.descriptor.get() != -1 || target.descriptor.get() != -1) {
        std::cerr << "WriteJob did not adopt target descriptor and snapshot atomically\n";
        return 1;
    }

    errno = 0;
    if (::fcntl(target_fd, F_GETFD) < 0) {
        std::cerr << "Adopted target descriptor is not open\n";
        return 1;
    }
    return 0;
}
