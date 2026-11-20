#include "image_validation.hpp"
#include "write_worker.hpp"

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

bool fail(const char* message) {
    std::cerr << message << '\n';
    return false;
}

bool write_fixture(int fd) {
    const char data[] = "abc";
    return ::write(fd, data, sizeof(data) - 1) ==
           static_cast<ssize_t>(sizeof(data) - 1);
}

iso_flash::WriteJob make_job(int image_fd, int target_fd) {
    return iso_flash::WriteJob(91, iso_flash::UniqueFd(image_fd),
                               iso_flash::UniqueFd(target_fd));
}

}  // namespace

int main() {
    char path[] = "/tmp/iso-flash-validation-XXXXXX";
    const int fixture_fd = ::mkstemp(path);
    if (fixture_fd < 0 || !write_fixture(fixture_fd)) {
        if (fixture_fd >= 0) ::close(fixture_fd);
        return fail("Cannot create image validation fixture") ? 0 : 1;
    }

    iso_flash::ImageSnapshot snapshot;
    std::string snapshot_error;
    if (!iso_flash::read_image_snapshot(
            fixture_fd, snapshot, snapshot_error,
            [](int fd, struct stat* metadata) { return ::fstat(fd, metadata); })) {
        ::close(fixture_fd);
        ::unlink(path);
        return fail("Cannot capture admission-time image snapshot") ? 0 : 1;
    }

    const int image_fd = ::open(path, O_RDONLY | O_CLOEXEC);
    int target_pipe[2] = {-1, -1};
    if (image_fd < 0 || ::pipe(target_pipe) != 0) {
        if (image_fd >= 0) ::close(image_fd);
        ::close(fixture_fd);
        ::unlink(path);
        return fail("Cannot create worker descriptors") ? 0 : 1;
    }
    ::close(target_pipe[1]);

    iso_flash::ImageValidation validation(snapshot);
    iso_flash::WriteWorker worker(
        make_job(image_fd, target_pipe[0]), snapshot.size_bytes,
        [&validation](iso_flash::WriteJob& job,
                      const iso_flash::WriteWorker::CancelCallback& cancelled) {
            return validation.validate(job.image_fd(), cancelled);
        },
        [&validation](iso_flash::WriteJob&,
                      const iso_flash::WriteWorker::ProgressCallback& progress,
                      const iso_flash::WriteWorker::CancelCallback&) {
            const auto digest = validation.result();
            if (!validation.is_valid() ||
                digest.sha256 !=
                    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") {
                return iso_flash::WriteOperationResult{
                    iso_flash::WriteIoStatus::Failed, 0, EIO};
            }
            if (!progress(3)) {
                return iso_flash::WriteOperationResult{
                    iso_flash::WriteIoStatus::Failed, 0, EIO};
            }
            return iso_flash::WriteOperationResult{
                iso_flash::WriteIoStatus::Completed, 3, 0};
        });

    if (!worker.start()) {
        ::close(fixture_fd);
        ::unlink(path);
        return fail("Validated worker did not start") ? 0 : 1;
    }
    worker.join();
    if (worker.snapshot().state != iso_flash::WriteJobState::Completed ||
        !validation.is_valid() || validation.error_number() != 0) {
        ::close(fixture_fd);
        ::unlink(path);
        return fail("Worker did not retain validated SHA-256 before operation") ? 0 : 1;
    }

    ::close(fixture_fd);
    ::unlink(path);

    char changed_path[] = "/tmp/iso-flash-validation-changed-XXXXXX";
    const int changed_fd = ::mkstemp(changed_path);
    if (changed_fd < 0 || !write_fixture(changed_fd)) {
        if (changed_fd >= 0) ::close(changed_fd);
        return fail("Cannot create mutation fixture") ? 0 : 1;
    }
    iso_flash::ImageSnapshot original;
    if (!iso_flash::read_image_snapshot(
            changed_fd, original, snapshot_error,
            [](int fd, struct stat* metadata) { return ::fstat(fd, metadata); })) {
        ::close(changed_fd);
        ::unlink(changed_path);
        return fail("Cannot snapshot mutation fixture") ? 0 : 1;
    }
    const int changed_image_fd = ::open(changed_path, O_RDONLY | O_CLOEXEC);
    int changed_target[2] = {-1, -1};
    if (changed_image_fd < 0 || ::pipe(changed_target) != 0) {
        if (changed_image_fd >= 0) ::close(changed_image_fd);
        ::close(changed_fd);
        ::unlink(changed_path);
        return fail("Cannot create mutation worker descriptors") ? 0 : 1;
    }
    ::close(changed_target[1]);
    if (::write(changed_fd, "x", 1) != 1) {
        ::close(changed_image_fd);
        ::close(changed_target[0]);
        ::close(changed_fd);
        ::unlink(changed_path);
        return fail("Cannot mutate image fixture") ? 0 : 1;
    }

    iso_flash::ImageValidation changed_validation(original);
    bool operation_called = false;
    iso_flash::WriteWorker changed_worker(
        make_job(changed_image_fd, changed_target[0]), original.size_bytes,
        [&changed_validation](iso_flash::WriteJob& job,
                              const iso_flash::WriteWorker::CancelCallback& cancelled) {
            return changed_validation.validate(job.image_fd(), cancelled);
        },
        [&operation_called](iso_flash::WriteJob&,
                            const iso_flash::WriteWorker::ProgressCallback&,
                            const iso_flash::WriteWorker::CancelCallback&) {
            operation_called = true;
            return iso_flash::WriteOperationResult{
                iso_flash::WriteIoStatus::Completed, 3, 0};
        });
    if (!changed_worker.start()) {
        ::close(changed_fd);
        ::unlink(changed_path);
        return fail("Mutation worker did not start") ? 0 : 1;
    }
    changed_worker.join();
    const auto failed = changed_worker.snapshot();
    ::close(changed_fd);
    ::unlink(changed_path);
    if (failed.state != iso_flash::WriteJobState::Failed ||
        failed.error_number != ESTALE || operation_called ||
        changed_validation.is_valid()) {
        return fail("Changed image was not rejected before operation") ? 0 : 1;
    }

    return 0;
}
