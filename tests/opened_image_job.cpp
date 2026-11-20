#include "image_source.hpp"
#include "write_job.hpp"

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>

int main() {
    char path[] = "/tmp/iso-flash-opened-image-XXXXXX";
    const int fixture = ::mkstemp(path);
    if (fixture < 0) {
        std::cerr << "Cannot create ISO fixture\n";
        return 1;
    }
    const char payload[] = "ISO-FLASH";
    if (::write(fixture, payload, sizeof(payload) - 1) !=
        static_cast<ssize_t>(sizeof(payload) - 1)) {
        ::close(fixture);
        ::unlink(path);
        std::cerr << "Cannot write ISO fixture\n";
        return 1;
    }
    ::close(fixture);

    iso_flash::OpenedImage opened;
    std::string error;
    if (!iso_flash::open_image_source(path, opened, error) || !opened.valid() ||
        opened.snapshot.size_bytes != sizeof(payload) - 1) {
        ::unlink(path);
        std::cerr << "Read-only ISO admission failed: " << error << '\n';
        return 1;
    }

    const int descriptor = opened.descriptor.get();
    const auto original_snapshot = opened.snapshot;
    int target_pipe[2] = {-1, -1};
    if (::pipe(target_pipe) != 0) {
        ::unlink(path);
        std::cerr << "Cannot create target descriptor fixture\n";
        return 1;
    }
    ::close(target_pipe[1]);

    iso_flash::WriteJob job(123, std::move(opened),
                            iso_flash::UniqueFd(target_pipe[0]));
    if (!job.has_valid_resources() || !job.has_image_snapshot() ||
        job.image_fd() != descriptor ||
        !iso_flash::same_image_snapshot(job.image_snapshot(), original_snapshot) ||
        opened.descriptor.get() != -1) {
        ::unlink(path);
        std::cerr << "WriteJob did not atomically adopt ISO descriptor and snapshot\n";
        return 1;
    }

    errno = 0;
    if (::fcntl(descriptor, F_GETFD) < 0) {
        ::unlink(path);
        std::cerr << "Adopted ISO descriptor is not open\n";
        return 1;
    }
    ::unlink(path);
    return 0;
}
