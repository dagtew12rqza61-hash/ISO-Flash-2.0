#include "image_fd_validation.hpp"
#include "write_operation.hpp"

#include <gio/gio.h>
#include <gio/gunixfdlist.h>

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr const char* kBusName = "org.iso_flash.Manager1";
constexpr const char* kObjectPath = "/org/iso_flash/Manager1";
constexpr const char* kInterface = "org.iso_flash.Manager1";

struct FinishedEvent {
    std::uint64_t id = 0;
    std::string state;
    std::uint64_t completed = 0;
    std::uint64_t total = 0;
    int error_number = 0;
    std::string message;
};

struct SignalState {
    std::uint64_t waiting_for = 0;
    GMainLoop* loop = nullptr;
    std::vector<FinishedEvent> finished;
    std::size_t progress_count = 0;
    bool timed_out = false;
};

void on_progress(GDBusConnection*, const gchar*, const gchar*,
                 const gchar*, const gchar*, GVariant* parameters,
                 gpointer user_data) {
    auto* state = static_cast<SignalState*>(user_data);
    guint64 job_id = 0;
    const gchar* job_state = nullptr;
    guint64 completed = 0;
    guint64 total = 0;
    g_variant_get(parameters, "(t&stt)", &job_id, &job_state, &completed, &total);
    (void)job_state;
    (void)completed;
    (void)total;
    ++state->progress_count;
}

void on_finished(GDBusConnection*, const gchar*, const gchar*,
                 const gchar*, const gchar*, GVariant* parameters,
                 gpointer user_data) {
    auto* state = static_cast<SignalState*>(user_data);
    guint64 job_id = 0;
    const gchar* job_state = nullptr;
    guint64 completed = 0;
    guint64 total = 0;
    gint error_number = 0;
    const gchar* message = nullptr;
    g_variant_get(parameters, "(t&stti&s)", &job_id, &job_state,
                  &completed, &total, &error_number, &message);
    state->finished.push_back({
        static_cast<std::uint64_t>(job_id),
        job_state != nullptr ? job_state : "",
        static_cast<std::uint64_t>(completed),
        static_cast<std::uint64_t>(total),
        error_number,
        message != nullptr ? message : ""});
    if (state->loop != nullptr && job_id == state->waiting_for) {
        g_main_loop_quit(state->loop);
    }
}

gboolean on_timeout(gpointer user_data) {
    auto* state = static_cast<SignalState*>(user_data);
    state->timed_out = true;
    if (state->loop != nullptr) g_main_loop_quit(state->loop);
    return G_SOURCE_REMOVE;
}

bool wait_for_finished(SignalState& state, std::uint64_t job_id,
                       FinishedEvent& result) {
    for (const auto& event : state.finished) {
        if (event.id == job_id) {
            result = event;
            return true;
        }
    }

    state.waiting_for = job_id;
    state.timed_out = false;
    state.loop = g_main_loop_new(nullptr, FALSE);
    const guint timeout_id = g_timeout_add_seconds(30, on_timeout, &state);
    g_main_loop_run(state.loop);
    if (!state.timed_out) g_source_remove(timeout_id);
    g_main_loop_unref(state.loop);
    state.loop = nullptr;

    for (const auto& event : state.finished) {
        if (event.id == job_id) {
            result = event;
            return true;
        }
    }
    return false;
}

GDBusConnection* new_system_bus_connection() {
    GError* error = nullptr;
    gchar* address = g_dbus_address_get_for_bus_sync(
        G_BUS_TYPE_SYSTEM, nullptr, &error);
    if (address == nullptr) {
        std::cerr << (error != nullptr ? error->message :
                      "Cannot resolve system D-Bus address") << '\n';
        if (error != nullptr) g_error_free(error);
        return nullptr;
    }
    GDBusConnection* connection = g_dbus_connection_new_for_address_sync(
        address,
        static_cast<GDBusConnectionFlags>(
            G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
            G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
        nullptr, nullptr, &error);
    g_free(address);
    if (connection == nullptr) {
        std::cerr << (error != nullptr ? error->message :
                      "Cannot create second D-Bus connection") << '\n';
        if (error != nullptr) g_error_free(error);
    }
    return connection;
}

bool start_write(GDBusConnection* connection, int image_fd,
                 guint32 device_major, guint32 device_minor,
                 std::uint64_t& job_id) {
    GError* error = nullptr;
    GUnixFDList* fd_list = g_unix_fd_list_new();
    const gint handle = g_unix_fd_list_append(fd_list, image_fd, &error);
    if (handle < 0) {
        std::cerr << (error != nullptr ? error->message :
                      "Cannot append ISO descriptor to Unix FD list") << '\n';
        if (error != nullptr) g_error_free(error);
        g_object_unref(fd_list);
        return false;
    }

    GVariant* reply = g_dbus_connection_call_with_unix_fd_list_sync(
        connection, kBusName, kObjectPath, kInterface, "StartWrite",
        g_variant_new("(huu)", handle, device_major, device_minor),
        G_VARIANT_TYPE("(bts)"), G_DBUS_CALL_FLAGS_NONE, 120000,
        fd_list, nullptr, nullptr, &error);
    g_object_unref(fd_list);
    if (reply == nullptr) {
        std::cerr << (error != nullptr ? error->message :
                      "StartWrite D-Bus call failed") << '\n';
        if (error != nullptr) g_error_free(error);
        return false;
    }

    gboolean accepted = FALSE;
    guint64 returned_id = 0;
    const gchar* reason = nullptr;
    g_variant_get(reply, "(bt&s)", &accepted, &returned_id, &reason);
    if (!accepted || returned_id == 0) {
        std::cerr << "StartWrite rejected: "
                  << (reason != nullptr ? reason : "(no reason)") << '\n';
        g_variant_unref(reply);
        return false;
    }
    job_id = static_cast<std::uint64_t>(returned_id);
    g_variant_unref(reply);
    return true;
}

bool cancel_write(GDBusConnection* connection, std::uint64_t job_id,
                  bool& accepted) {
    GError* error = nullptr;
    GVariant* reply = g_dbus_connection_call_sync(
        connection, kBusName, kObjectPath, kInterface, "CancelWrite",
        g_variant_new("(t)", static_cast<guint64>(job_id)),
        G_VARIANT_TYPE("(bs)"), G_DBUS_CALL_FLAGS_NONE, 5000,
        nullptr, &error);
    if (reply == nullptr) {
        std::cerr << (error != nullptr ? error->message :
                      "CancelWrite D-Bus call failed") << '\n';
        if (error != nullptr) g_error_free(error);
        return false;
    }
    const gchar* reason = nullptr;
    gboolean value = FALSE;
    g_variant_get(reply, "(b&s)", &value, &reason);
    accepted = value != FALSE;
    g_variant_unref(reply);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 6) {
        std::cerr << "Usage: test_dbus_loop_write IMAGE DEVICE BACKING_FILE DISCONNECT_IMAGE REMOVE_MARKER\n";
        return 2;
    }

    const int disconnect_image_fd =
        ::open(argv[4], O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (disconnect_image_fd < 0) {
        std::cerr << "Cannot open disconnect test ISO read-only\\n";
        return 2;
    }
    const int image_fd = ::open(argv[1], O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (image_fd < 0) {
        ::close(disconnect_image_fd);
        std::cerr << "Cannot open test ISO read-only\n";
        return 2;
    }
    struct stat device_metadata{};
    if (::stat(argv[2], &device_metadata) != 0 ||
        !S_ISBLK(device_metadata.st_mode)) {
        std::cerr << "Test target is not a block device\n";
        ::close(image_fd);
        return 2;
    }

    const auto image = iso_flash::validate_image_fd(image_fd);
    if (!image.valid) {
        std::cerr << "Test ISO validation failed: " << image.reason << '\n';
        ::close(image_fd);
        return 2;
    }

    GError* error = nullptr;
    GDBusConnection* owner = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
    if (owner == nullptr) {
        std::cerr << (error != nullptr ? error->message :
                      "Cannot connect owner D-Bus client") << '\n';
        if (error != nullptr) g_error_free(error);
        ::close(image_fd);
        return 2;
    }
    GDBusConnection* other = new_system_bus_connection();
    if (other == nullptr) {
        g_object_unref(owner);
        ::close(image_fd);
        return 2;
    }

    SignalState signals;
    const guint progress_subscription = g_dbus_connection_signal_subscribe(
        owner, nullptr, kInterface, "Progress", kObjectPath, nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE, on_progress, &signals, nullptr);
    const guint finished_subscription = g_dbus_connection_signal_subscribe(
        owner, nullptr, kInterface, "Finished", kObjectPath, nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE, on_finished, &signals, nullptr);
    if (progress_subscription == 0 || finished_subscription == 0) {
        std::cerr << "Cannot subscribe to write-job signals\n";
        if (progress_subscription != 0)
            g_dbus_connection_signal_unsubscribe(owner, progress_subscription);
        if (finished_subscription != 0)
            g_dbus_connection_signal_unsubscribe(owner, finished_subscription);
        g_object_unref(other);
        g_object_unref(owner);
        ::close(image_fd);
        return 2;
    }

    const guint32 device_major = static_cast<guint32>(major(device_metadata.st_rdev));
    const guint32 device_minor = static_cast<guint32>(minor(device_metadata.st_rdev));
    std::uint64_t cancelled_job_id = 0;
    if (!start_write(owner, image_fd, device_major, device_minor,
                     cancelled_job_id)) {
        return 3;
    }

    bool wrong_owner_cancel = true;
    if (!cancel_write(other, cancelled_job_id, wrong_owner_cancel) ||
        wrong_owner_cancel) {
        std::cerr << "A different D-Bus client was allowed to cancel the job\n";
        return 4;
    }

    bool owner_cancel = false;
    if (!cancel_write(owner, cancelled_job_id, owner_cancel) || !owner_cancel) {
        std::cerr << "The owning D-Bus client could not cancel its job\n";
        return 4;
    }

    FinishedEvent cancelled_event;
    if (!wait_for_finished(signals, cancelled_job_id, cancelled_event) ||
        cancelled_event.state != "cancelled" ||
        cancelled_event.completed != 0) {
        std::cerr << "Cancelled loop-device write did not finish safely\n";
        return 4;
    }

    g_dbus_connection_close_sync(other, nullptr, nullptr);
    g_object_unref(other);
    other = nullptr;

    GDBusConnection* disconnect_owner = new_system_bus_connection();
    if (disconnect_owner == nullptr) return 5;
    std::uint64_t disconnected_job_id = 0;
    const auto disconnect_image =
        iso_flash::validate_image_fd(disconnect_image_fd);
    if (!disconnect_image.valid ||
        !start_write(disconnect_owner, disconnect_image_fd,
                     device_major, device_minor, disconnected_job_id)) {
        g_object_unref(disconnect_owner);
        return 5;
    }
    g_dbus_connection_close_sync(disconnect_owner, nullptr, nullptr);
    g_object_unref(disconnect_owner);
    g_usleep(1500000);

    const int pre_success_backing_fd =
        ::open(argv[3], O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (pre_success_backing_fd < 0) {
        ::close(disconnect_image_fd);
        return 5;
    }
    std::vector<unsigned char> zero_check(1024U * 1024U);
    std::uint64_t zero_offset = 0;
    bool backing_unchanged = true;
    while (zero_offset < disconnect_image.snapshot.size_bytes) {
        const std::uint64_t remaining =
            disconnect_image.snapshot.size_bytes - zero_offset;
        const std::size_t requested = static_cast<std::size_t>(
            remaining < zero_check.size() ? remaining : zero_check.size());
        const ssize_t count = ::pread(
            pre_success_backing_fd, zero_check.data(), requested,
            static_cast<off_t>(zero_offset));
        if (count <= 0 ||
            static_cast<std::size_t>(count) != requested) {
            backing_unchanged = false;
            break;
        }
        for (std::size_t i = 0; i < requested; ++i) {
            if (zero_check[i] != 0) {
                backing_unchanged = false;
                break;
            }
        }
        if (!backing_unchanged) break;
        zero_offset += static_cast<std::uint64_t>(count);
    }
    ::close(pre_success_backing_fd);
    ::close(disconnect_image_fd);
    if (!backing_unchanged) {
        std::cerr << "Disconnected D-Bus client was not cancelled before its first write\\n";
        return 5;
    }

    std::uint64_t completed_job_id = 0;
    if (!start_write(owner, image_fd, device_major, device_minor,
                     completed_job_id)) {
        return 5;
    }
    FinishedEvent completed_event;
    if (!wait_for_finished(signals, completed_job_id, completed_event) ||
        completed_event.state != "completed" ||
        completed_event.error_number != 0 ||
        completed_event.completed != image.snapshot.size_bytes ||
        completed_event.total != image.snapshot.size_bytes ||
        signals.progress_count == 0) {
        std::cerr << "Loop-device write did not complete with verified progress\n";
        return 5;
    }

    const int backing_fd = ::open(argv[3], O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (backing_fd < 0) {
        std::cerr << "Cannot open loop-device backing file for read-back\n";
        return 6;
    }
    const auto readback = iso_flash::hash_fd_range_sha256(
        backing_fd, image.snapshot.size_bytes, [] { return false; });
    ::close(backing_fd);
    if (readback.status != iso_flash::ImageHashStatus::Completed ||
        readback.sha256 != image.sha256) {
        std::cerr << "Loop backing-file SHA-256 does not match the source ISO\n";
        return 6;
    }

    const int removal_image_fd =
        ::open(argv[4], O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (removal_image_fd < 0) {
        std::cerr << "Cannot reopen removal-test ISO\n";
        return 7;
    }
    std::uint64_t removal_job_id = 0;
    if (!start_write(owner, removal_image_fd, device_major, device_minor,
                     removal_job_id)) {
        ::close(removal_image_fd);
        return 7;
    }
    const int marker_fd = ::open(
        argv[5], O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (marker_fd < 0) {
        ::close(removal_image_fd);
        std::cerr << "Cannot create device-removal synchronization marker\n";
        return 7;
    }
    ::close(marker_fd);

    FinishedEvent removal_event;
    const bool removal_finished =
        wait_for_finished(signals, removal_job_id, removal_event);
    ::close(removal_image_fd);
    if (!removal_finished ||
        removal_event.state != "device-lost" ||
        removal_event.error_number != ENODEV ||
        removal_event.completed != 0) {
        std::cerr << "Detached loop device was not reported as a terminal device-loss event\n";
        return 7;
    }

    const int unchanged_backing_fd =
        ::open(argv[3], O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (unchanged_backing_fd < 0) {
        return 7;
    }
    const auto unchanged_readback = iso_flash::hash_fd_range_sha256(
        unchanged_backing_fd, image.snapshot.size_bytes, [] { return false; });
    ::close(unchanged_backing_fd);
    if (unchanged_readback.status != iso_flash::ImageHashStatus::Completed ||
        unchanged_readback.sha256 != image.sha256) {
        std::cerr << "Detached loop device changed the backing image after removal\n";
        return 7;
    }

    std::cout << "Loop-device D-Bus write, owner checks, cancellation, fsync, "
                 "progress, Finished signal, and SHA-256 read-back passed\n";

    g_dbus_connection_signal_unsubscribe(owner, progress_subscription);
    g_dbus_connection_signal_unsubscribe(owner, finished_subscription);
    if (other != nullptr) g_object_unref(other);
    g_object_unref(owner);
    ::close(image_fd);
    return 0;
}
