#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
bool validate_target(const std::string&, std::string&);
static int fail(const std::string& s) { std::cerr << "ERROR: " << s << "\n"; return 1; }
int main(int argc, char** argv) {
    if (argc != 4 || std::string(argv[1]) != "--write") return fail("Usage: iso-flash-write --write IMAGE DEVICE");
    const std::string image = argv[2], device = argv[3]; struct stat image_st{};
    if (::stat(image.c_str(), &image_st) != 0 || !S_ISREG(image_st.st_mode) || image_st.st_size <= 0) return fail("Invalid or empty image file");
    std::string reason; if (!validate_target(device, reason)) return fail(reason);
    const int in = ::open(image.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (in < 0) return fail("Cannot open image: " + std::string(std::strerror(errno)));
    const int out = ::open(device.c_str(), O_WRONLY | O_CLOEXEC | O_EXCL | O_NOFOLLOW);
    if (out < 0) { const std::string e = std::strerror(errno); ::close(in); return fail("Cannot exclusively open target: " + e); }
    unsigned long long capacity = 0;
    if (::ioctl(out, BLKGETSIZE64, &capacity) != 0 || static_cast<unsigned long long>(image_st.st_size) > capacity) { ::close(in); ::close(out); return fail("Image exceeds target capacity or capacity query failed"); }
    std::vector<char> buffer(4U * 1024U * 1024U); unsigned long long written = 0;
    for (;;) {
        const ssize_t n = ::read(in, buffer.data(), buffer.size());
        if (n == 0) break;
        if (n < 0) { if (errno == EINTR) continue; const std::string e = std::strerror(errno); ::close(in); ::close(out); return fail("Image read failed: " + e); }
        ssize_t offset = 0;
        while (offset < n) {
            const ssize_t w = ::write(out, buffer.data() + offset, static_cast<size_t>(n - offset));
            if (w < 0) { if (errno == EINTR) continue; const std::string e = std::strerror(errno); ::close(in); ::close(out); return fail("Device write failed: " + e); }
            if (w == 0) { ::close(in); ::close(out); return fail("Zero-length device write"); }
            offset += w;
        }
        written += static_cast<unsigned long long>(n); std::cerr << "PROGRESS " << written << " " << image_st.st_size << "\n";
    }
    if (::fsync(out) != 0) { const std::string e = std::strerror(errno); ::close(in); ::close(out); return fail("fsync failed: " + e); }
    ::close(in); ::close(out); std::cout << "WRITE_COMPLETE " << written << "\n"; return 0;
}
