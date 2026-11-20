#include "write_job.hpp"

#include <cerrno>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>
#include <utility>

int main() {
    int image_pipe[2] = {-1, -1};
    int target_pipe[2] = {-1, -1};
    if (::pipe(image_pipe) != 0 || ::pipe(target_pipe) != 0) {
        std::cerr << "Cannot create descriptor test pipes\n";
        return 1;
    }

    const int image_fd = image_pipe[0];
    const int target_fd = target_pipe[0];
    {
        iso_flash::WriteJob job(7, iso_flash::UniqueFd(image_fd),
                                iso_flash::UniqueFd(target_fd));
        if (!job.has_valid_resources() || job.id() != 7 ||
            job.image_fd() != image_fd || job.target_fd() != target_fd ||
            job.state() != iso_flash::WriteJobState::Queued) {
            std::cerr << "Write job did not retain its descriptors\n";
            return 1;
        }

        iso_flash::WriteJob moved(std::move(job));
        if (!moved.has_valid_resources() || moved.image_fd() != image_fd ||
            moved.target_fd() != target_fd || job.image_fd() != -1 ||
            job.target_fd() != -1) {
            std::cerr << "Moving a write job did not transfer descriptor ownership\n";
            return 1;
        }
        if (!moved.cancel() ||
            moved.state() != iso_flash::WriteJobState::Cancelled) {
            std::cerr << "Queued write job cancellation failed\n";
            return 1;
        }
    }

    int writable_image_pipe[2] = {-1, -1};
    int writable_target_pipe[2] = {-1, -1};
    if (::pipe(writable_image_pipe) != 0 ||
        ::pipe(writable_target_pipe) != 0) {
        std::cerr << "Cannot create writable-target ownership pipes\\n";
        return 1;
    }
    ::close(writable_image_pipe[1]);
    ::close(writable_target_pipe[1]);
    iso_flash::ImageSnapshot image_snapshot{};
    image_snapshot.size_bytes = 4096;
    iso_flash::OpenedImage opened_image(
        iso_flash::UniqueFd(writable_image_pipe[0]), image_snapshot);
    iso_flash::DeviceSnapshot device_snapshot{};
    device_snapshot.device_number = 1;
    device_snapshot.capacity_bytes = 8192;
    device_snapshot.read_only = false;
    iso_flash::WritableTarget writable_target(
        iso_flash::UniqueFd(writable_target_pipe[0]), device_snapshot,
        "/sys/devices/virtual/block/loop0", "/dev/loop0");
    {
        iso_flash::WriteJob job(8, std::move(opened_image),
                                std::move(writable_target));
        if (!job.has_valid_resources() || !job.has_image_snapshot() ||
            !job.has_target_snapshot() || job.target_fd() < 0 ||
            job.target_snapshot().capacity_bytes != 8192 ||
            job.writable_target().node_path != "/dev/loop0") {
            std::cerr << "Write job did not retain its writable target identity\\n";
            return 1;
        }
    }

    errno = 0;
    const bool image_closed = ::fcntl(image_fd, F_GETFD) == -1 && errno == EBADF;
    errno = 0;
    const bool target_closed = ::fcntl(target_fd, F_GETFD) == -1 && errno == EBADF;

    ::close(image_pipe[1]);
    ::close(target_pipe[1]);
    if (!image_closed || !target_closed) {
        if (!image_closed) {
            ::close(image_fd);
        }
        if (!target_closed) {
            ::close(target_fd);
        }
        std::cerr << "Write job did not release owned descriptors\n";
        return 1;
    }
    return 0;
}
