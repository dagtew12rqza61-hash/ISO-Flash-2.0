#include "write_operation.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

bool create_temp_file(int& fd, std::string& path) {
    char name[] = "/tmp/iso-flash-write-operation-XXXXXX";
    fd = ::mkstemp(name);
    if (fd < 0) return false;
    path = name;
    return true;
}

void remove_temp_file(int fd, const std::string& path) {
    if (fd >= 0) ::close(fd);
    if (!path.empty()) ::unlink(path.c_str());
}

bool write_all(int fd, const std::vector<unsigned char>& bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count = ::pwrite(
            fd, bytes.data() + offset, bytes.size() - offset,
            static_cast<off_t>(offset));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        offset += static_cast<std::size_t>(count);
    }
    return true;
}

bool snapshot_file(int fd, iso_flash::ImageSnapshot& snapshot) {
    std::string error;
    return iso_flash::read_image_snapshot(
        fd, snapshot, error,
        [](int descriptor, struct stat* metadata) {
            return ::fstat(descriptor, metadata);
        });
}

}  // namespace

int main() {
    int image_fd = -1;
    int target_fd = -1;
    std::string image_path;
    std::string target_path;
    if (!create_temp_file(image_fd, image_path) ||
        !create_temp_file(target_fd, target_path)) {
        std::cerr << "Cannot create write-operation test files\n";
        remove_temp_file(image_fd, image_path);
        remove_temp_file(target_fd, target_path);
        return 1;
    }

    std::vector<unsigned char> image(2U * 1024U * 1024U + 317U);
    for (std::size_t i = 0; i < image.size(); ++i) {
        image[i] = static_cast<unsigned char>((i * 37U + 11U) & 0xffU);
    }
    if (!write_all(image_fd, image) ||
        ::ftruncate(target_fd, static_cast<off_t>(image.size())) != 0) {
        std::cerr << "Cannot prepare test data\n";
        remove_temp_file(image_fd, image_path);
        remove_temp_file(target_fd, target_path);
        return 1;
    }

    iso_flash::ImageSnapshot snapshot{};
    if (!snapshot_file(image_fd, snapshot)) {
        std::cerr << "Cannot snapshot test image\n";
        remove_temp_file(image_fd, image_path);
        remove_temp_file(target_fd, target_path);
        return 1;
    }
    const auto expected = iso_flash::hash_image_sha256(
        image_fd, snapshot, [] { return false; });
    if (expected.status != iso_flash::ImageHashStatus::Completed) {
        std::cerr << "Cannot hash test image\n";
        remove_temp_file(image_fd, image_path);
        remove_temp_file(target_fd, target_path);
        return 1;
    }

    std::uint64_t last_progress = 0;
    const auto success = iso_flash::write_image_and_verify(
        image_fd, target_fd, snapshot, image.size(), expected.sha256,
        [] { return 0; },
        [&last_progress](std::uint64_t value) {
            if (value < last_progress) return false;
            last_progress = value;
            return true;
        },
        [] { return false; });
    if (success.status != iso_flash::WriteIoStatus::Completed ||
        !success.verified || success.bytes_transferred != image.size() ||
        success.readback_sha256 != expected.sha256 ||
        last_progress != image.size()) {
        std::cerr << "Write, fsync, and SHA-256 read-back verification failed\n";
        remove_temp_file(image_fd, image_path);
        remove_temp_file(target_fd, target_path);
        return 1;
    }

    const auto mismatch = iso_flash::write_image_and_verify(
        image_fd, target_fd, snapshot, image.size(), std::string(64, '0'),
        [] { return 0; }, [](std::uint64_t) { return true; },
        [] { return false; });
    if (mismatch.status != iso_flash::WriteIoStatus::Failed ||
        mismatch.error_number != EILSEQ || mismatch.verified) {
        std::cerr << "Read-back digest mismatch was not rejected\n";
        remove_temp_file(image_fd, image_path);
        remove_temp_file(target_fd, target_path);
        return 1;
    }

    if (::ftruncate(target_fd, static_cast<off_t>(image.size())) != 0) {
        remove_temp_file(image_fd, image_path);
        remove_temp_file(target_fd, target_path);
        return 1;
    }
    const auto cancelled = iso_flash::write_image_and_verify(
        image_fd, target_fd, snapshot, image.size(), expected.sha256,
        [] { return 0; }, [](std::uint64_t) { return true; },
        [] { return true; });
    if (cancelled.status != iso_flash::WriteIoStatus::Cancelled ||
        cancelled.bytes_transferred != 0) {
        std::cerr << "Pre-write cancellation was not honored\n";
        remove_temp_file(image_fd, image_path);
        remove_temp_file(target_fd, target_path);
        return 1;
    }

    const auto lost = iso_flash::write_image_and_verify(
        image_fd, target_fd, snapshot, image.size(), expected.sha256,
        [] { return ENODEV; }, [](std::uint64_t) { return true; },
        [] { return false; });
    if (lost.status != iso_flash::WriteIoStatus::Failed ||
        lost.error_number != ENODEV) {
        std::cerr << "Pre-write device loss was not preserved\n";
        remove_temp_file(image_fd, image_path);
        remove_temp_file(target_fd, target_path);
        return 1;
    }

    remove_temp_file(image_fd, image_path);
    remove_temp_file(target_fd, target_path);
    return 0;
}
