#include "dbus_caller_watch.hpp"
#include "device_guard.hpp"
#include "device_io.hpp"
#include "image_fd_validation.hpp"
#include "image_validation_limiter.hpp"
#include "image_validation_task.hpp"
#include "polkit_authorization.hpp"
#include "image_source.hpp"
#include "write_job_registry.hpp"
#include "write_operation.hpp"
#include "write_worker.hpp"
#include "writable_target.hpp"

#include <gio/gio.h>
#include <gio/gunixfdlist.h>

#include <atomic>
#include <cerrno>
#ifdef ISO_FLASH_TEST_WRITE_DELAY
#include <chrono>
#include <thread>
#endif
#include <cstdint>
#include <exception>
#include <memory>
#include <new>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <sys/sysmacros.h>
#include <unistd.h>

namespace {
constexpr const char* kBusName = "org.iso_flash.Manager1";
constexpr const char* kObjectPath = "/org/iso_flash/Manager1";
iso_flash::ImageValidationLimiter g_image_validation_limiter(2);
iso_flash::WriteJobRegistry g_write_jobs;
std::atomic<std::uint64_t> g_next_job_id{1};
std::unordered_map<std::uint64_t, iso_flash::WriteJobSnapshot> g_last_emitted;

constexpr const char* kIntrospectionXml = R"xml(
<node>
  <interface name="org.iso_flash.Manager1">
    <method name="InspectTarget">
      <arg name="major" type="u" direction="in"/>
      <arg name="minor" type="u" direction="in"/>
      <arg name="accepted" type="b" direction="out"/>
      <arg name="capacity_bytes" type="t" direction="out"/>
      <arg name="reason" type="s" direction="out"/>
    </method>
    <method name="CheckWriteAuthorization">
      <arg name="authorized" type="b" direction="out"/>
      <arg name="uid" type="u" direction="out"/>
      <arg name="pid" type="u" direction="out"/>
      <arg name="process_start_ticks" type="t" direction="out"/>
      <arg name="reason" type="s" direction="out"/>
    </method>
    <method name="ValidateImage">
      <arg name="image_fd" type="h" direction="in"/>
      <arg name="accepted" type="b" direction="out"/>
      <arg name="size_bytes" type="t" direction="out"/>
      <arg name="sha256" type="s" direction="out"/>
      <arg name="reason" type="s" direction="out"/>
    </method>
    <method name="StartWrite">
      <arg name="image_fd" type="h" direction="in"/>
      <arg name="major" type="u" direction="in"/>
      <arg name="minor" type="u" direction="in"/>
      <arg name="accepted" type="b" direction="out"/>
      <arg name="job_id" type="t" direction="out"/>
      <arg name="reason" type="s" direction="out"/>
    </method>
    <method name="CancelWrite">
      <arg name="job_id" type="t" direction="in"/>
      <arg name="accepted" type="b" direction="out"/>
      <arg name="reason" type="s" direction="out"/>
    </method>
    <signal name="Progress">
      <arg name="job_id" type="t"/>
      <arg name="state" type="s"/>
      <arg name="completed_bytes" type="t"/>
      <arg name="total_bytes" type="t"/>
    </signal>
    <signal name="Finished">
      <arg name="job_id" type="t"/>
      <arg name="state" type="s"/>
      <arg name="completed_bytes" type="t"/>
      <arg name="total_bytes" type="t"/>
      <arg name="error_number" type="i"/>
      <arg name="message" type="s"/>
    </signal>
  </interface>
</node>
)xml";

void return_inspection(GDBusMethodInvocation* invocation, bool accepted,
                       std::uint64_t capacity, const std::string& reason) {
    g_dbus_method_invocation_return_value(
        invocation,
        g_variant_new("(bts)", accepted, static_cast<guint64>(capacity),
                      reason.c_str()));
}

struct AuthorizationTaskData {
    GDBusConnection* connection = nullptr;
    GCancellable* cancellable = nullptr;
    guint disconnect_subscription = 0;
    std::string sender;
    iso_flash::PolkitAuthorizationResult result;

    ~AuthorizationTaskData() {
        if (connection != nullptr && disconnect_subscription != 0) {
            g_dbus_connection_signal_unsubscribe(connection, disconnect_subscription);
        }
        if (connection != nullptr) g_object_unref(connection);
        if (cancellable != nullptr) g_object_unref(cancellable);
    }
};

void authorization_worker(GTask* task, gpointer, gpointer task_data,
                          GCancellable* cancellable) {
    auto* data = static_cast<AuthorizationTaskData*>(task_data);
    data->result = iso_flash::authorize_dbus_sender(
        data->connection, data->sender.c_str(), cancellable);
    if (cancellable != nullptr && g_cancellable_is_cancelled(cancellable)) {
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                "D-Bus caller disconnected during Polkit authorization");
        return;
    }
    g_task_return_boolean(task, TRUE);
}

void authorization_complete(GObject*, GAsyncResult* async_result,
                            gpointer user_data) {
    auto* invocation = G_DBUS_METHOD_INVOCATION(user_data);
    GTask* task = G_TASK(async_result);
    auto* data = static_cast<AuthorizationTaskData*>(g_task_get_task_data(task));
    GError* error = nullptr;
    if (!g_task_propagate_boolean(task, &error)) {
        const std::string reason = error != nullptr ? error->message :
            "Polkit authorization task failed";
        if (error != nullptr) g_error_free(error);
        g_dbus_method_invocation_return_value(
            invocation, g_variant_new("(buuts)", FALSE, 0U, 0U,
                                      static_cast<guint64>(0), reason.c_str()));
    } else {
        const auto& caller = data->result.caller;
        g_dbus_method_invocation_return_value(
            invocation,
            g_variant_new("(buuts)", data->result.authorized,
                          caller.uid, caller.pid,
                          static_cast<guint64>(caller.process_start_ticks),
                          data->result.reason.c_str()));
    }
    g_object_unref(invocation);
}

struct ImageValidationTaskData {
    iso_flash::UniqueFd descriptor;
    iso_flash::ImageValidationLimiter::Lease capacity_lease;
    GCancellable* cancellable = nullptr;
    GDBusConnection* connection = nullptr;
    guint disconnect_subscription = 0;
    iso_flash::ImageFdValidationResult validation;
    std::string sender;

    ~ImageValidationTaskData() {
        if (connection != nullptr && disconnect_subscription != 0) {
            g_dbus_connection_signal_unsubscribe(connection, disconnect_subscription);
        }
        if (connection != nullptr) g_object_unref(connection);
        if (cancellable != nullptr) g_object_unref(cancellable);
    }
};

void image_validation_worker(GTask* task, gpointer, gpointer task_data,
                             GCancellable* cancellable) {
    auto* data = static_cast<ImageValidationTaskData*>(task_data);
    auto outcome = iso_flash::run_image_validation_task(
        data->descriptor.get(),
        [cancellable](int fd) {
            return iso_flash::validate_image_fd(
                fd, [cancellable] {
                    return cancellable != nullptr &&
                           g_cancellable_is_cancelled(cancellable);
                });
        });
    if (!outcome.completed) {
        g_task_return_new_error(
            task, G_IO_ERROR, G_IO_ERROR_FAILED, "Image validation failed: %s",
            outcome.error.c_str());
        return;
    }
    data->validation = std::move(outcome.validation);
    g_task_return_boolean(task, TRUE);
}

void image_validation_complete(GObject*, GAsyncResult* async_result,
                               gpointer user_data) {
    auto* invocation = G_DBUS_METHOD_INVOCATION(user_data);
    GTask* task = G_TASK(async_result);
    auto* data = static_cast<ImageValidationTaskData*>(g_task_get_task_data(task));

    GError* error = nullptr;
    if (!g_task_propagate_boolean(task, &error)) {
        const std::string reason = error != nullptr
            ? error->message : "Asynchronous image validation failed";
        if (error != nullptr) g_error_free(error);
        g_dbus_method_invocation_return_value(
            invocation,
            g_variant_new("(btss)", FALSE, static_cast<guint64>(0), "",
                          reason.c_str()));
    } else {
        const auto& validation = data->validation;
        g_dbus_method_invocation_return_value(
            invocation,
            g_variant_new("(btss)", validation.valid,
                          static_cast<guint64>(validation.bytes_hashed),
                          validation.sha256.c_str(),
                          validation.valid ? "" : validation.reason.c_str()));
    }
    g_object_unref(invocation);
}

struct StartWriteTaskData {
    iso_flash::UniqueFd descriptor;
    iso_flash::ImageValidationLimiter::Lease capacity_lease;
    GDBusConnection* connection = nullptr;
    GCancellable* cancellable = nullptr;
    guint disconnect_subscription = 0;
    std::string sender;
    dev_t expected_device = 0;
    iso_flash::ImageFdValidationResult image_validation;
    std::uint64_t job_id = 0;
    bool accepted = false;
    std::string reason;

    ~StartWriteTaskData() {
        if (connection != nullptr && disconnect_subscription != 0) {
            g_dbus_connection_signal_unsubscribe(connection, disconnect_subscription);
        }
        if (connection != nullptr) g_object_unref(connection);
        if (cancellable != nullptr) g_object_unref(cancellable);
    }
};

void complete_start_write_failure(GTask* task, StartWriteTaskData* data,
                                  const std::string& reason) {
    data->accepted = false;
    data->job_id = 0;
    data->reason = reason;
    g_task_return_boolean(task, TRUE);
}

void start_write_worker(GTask* task, gpointer, gpointer task_data,
                        GCancellable* cancellable) {
    auto* data = static_cast<StartWriteTaskData*>(task_data);
    try {
        const auto is_cancelled = [cancellable] {
            return cancellable != nullptr &&
                   g_cancellable_is_cancelled(cancellable);
        };

        if (is_cancelled()) {
            complete_start_write_failure(
                task, data, "D-Bus caller disconnected before image validation");
            return;
        }

        data->image_validation = iso_flash::validate_image_fd(
            data->descriptor.get(), is_cancelled);
        if (!data->image_validation.valid) {
            complete_start_write_failure(
                task, data, data->image_validation.reason.empty()
                    ? "ISO descriptor validation failed"
                    : data->image_validation.reason);
            return;
        }

        if (is_cancelled()) {
            complete_start_write_failure(
                task, data, "D-Bus caller disconnected before authorization");
            return;
        }

        const auto authorization = iso_flash::authorize_dbus_sender(
            data->connection, data->sender.c_str(), cancellable);
        if (!authorization.authorized) {
            complete_start_write_failure(
                task, data, authorization.reason.empty()
                    ? "Polkit denied write authorization"
                    : authorization.reason);
            return;
        }

        if (is_cancelled()) {
            complete_start_write_failure(
                task, data, "D-Bus caller disconnected during authorization");
            return;
        }

        iso_flash::WritableTarget target;
        std::string target_error;
        if (!iso_flash::open_writable_target_source(
                data->expected_device, target, target_error)) {
            complete_start_write_failure(
                task, data, target_error.empty()
                    ? "Target failed safety validation after authorization"
                    : target_error);
            return;
        }

        if (data->image_validation.snapshot.size_bytes >
            target.snapshot.capacity_bytes) {
            complete_start_write_failure(
                task, data, "ISO image is larger than the selected target");
            return;
        }

        if (is_cancelled()) {
            complete_start_write_failure(
                task, data, "D-Bus caller disconnected before write-job admission");
            return;
        }

        const std::uint64_t job_id = g_next_job_id.fetch_add(1);
        if (job_id == 0) {
            complete_start_write_failure(task, data, "Write-job identifier space exhausted");
            return;
        }
        const std::string expected_sha256 = data->image_validation.sha256;
        const std::uint64_t image_size = data->image_validation.snapshot.size_bytes;

        iso_flash::OpenedImage image(
            std::move(data->descriptor), data->image_validation.snapshot);
        iso_flash::WriteJob job(job_id, std::move(image), std::move(target));

        auto validation = [expected_sha256](
            iso_flash::WriteJob& write_job,
            const iso_flash::WriteWorker::CancelCallback& cancelled) {
            if (cancelled()) return ECANCELED;
            const auto current_image = iso_flash::validate_image_fd(
                write_job.image_fd(), cancelled);
            if (!current_image.valid) {
                return current_image.error_number == 0
                    ? EIO : current_image.error_number;
            }
            if (!iso_flash::same_image_snapshot(
                    write_job.image_snapshot(), current_image.snapshot) ||
                current_image.sha256 != expected_sha256) {
                return ESTALE;
            }
            if (cancelled()) return ECANCELED;
            std::string error;
            if (!iso_flash::revalidate_writable_target_before_first_write(
                    write_job.writable_target(), error)) {
                return EBUSY;
            }
            return 0;
        };

        auto operation = [expected_sha256](
            iso_flash::WriteJob& write_job,
            const iso_flash::WriteWorker::ProgressCallback& progress,
            const iso_flash::WriteWorker::CancelCallback& cancelled) {
#ifdef ISO_FLASH_TEST_WRITE_DELAY
            for (int delay_step = 0; delay_step < 10; ++delay_step) {
                if (cancelled()) {
                    return iso_flash::WriteOperationResult{
                        iso_flash::WriteIoStatus::Cancelled, 0, 0};
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
#endif
            const auto outcome = iso_flash::write_image_and_verify(
                write_job.image_fd(), write_job.target_fd(),
                write_job.image_snapshot(),
                write_job.target_snapshot().capacity_bytes,
                expected_sha256,
                [&write_job] {
                    std::string error;
                    return iso_flash::revalidate_writable_target_before_first_write(
                        write_job.writable_target(), error) ? 0 : EBUSY;
                },
                progress, cancelled);
            return iso_flash::WriteOperationResult{
                outcome.status, outcome.bytes_transferred, outcome.error_number};
        };

        auto worker = std::make_unique<iso_flash::WriteWorker>(
            std::move(job), image_size, std::move(validation),
            std::move(operation));
        if (!g_write_jobs.add(data->sender, std::move(worker))) {
            complete_start_write_failure(
                task, data, "Cannot admit the write job into the service registry");
            return;
        }

        if (is_cancelled()) {
            (void)g_write_jobs.cancel(data->sender, job_id);
            complete_start_write_failure(
                task, data, "D-Bus caller disconnected during write-job admission");
            return;
        }

        data->job_id = job_id;
        data->accepted = true;
        data->reason.clear();
        g_task_return_boolean(task, TRUE);
    } catch (const std::exception& exception) {
        complete_start_write_failure(
            task, data, std::string("Cannot create write job: ") + exception.what());
    } catch (...) {
        complete_start_write_failure(
            task, data, "Unexpected error while creating write job");
    }
}

void start_write_complete(GObject*, GAsyncResult* async_result,
                          gpointer user_data) {
    auto* invocation = G_DBUS_METHOD_INVOCATION(user_data);
    GTask* task = G_TASK(async_result);
    auto* data = static_cast<StartWriteTaskData*>(g_task_get_task_data(task));
    GError* error = nullptr;
    if (!g_task_propagate_boolean(task, &error)) {
        const std::string reason = error != nullptr
            ? error->message : "Asynchronous StartWrite task failed";
        if (error != nullptr) g_error_free(error);
        g_dbus_method_invocation_return_value(
            invocation, g_variant_new("(bts)", FALSE,
                                      static_cast<guint64>(0), reason.c_str()));
    } else {
        g_dbus_method_invocation_return_value(
            invocation,
            g_variant_new("(bts)", data->accepted,
                          static_cast<guint64>(data->job_id),
                          data->reason.c_str()));
    }
    g_object_unref(invocation);
}

const char* write_state_name(iso_flash::WriteJobState state) {
    switch (state) {
    case iso_flash::WriteJobState::Queued: return "queued";
    case iso_flash::WriteJobState::Validating: return "validating";
    case iso_flash::WriteJobState::Writing: return "writing";
    case iso_flash::WriteJobState::Cancelling: return "cancelling";
    case iso_flash::WriteJobState::Completed: return "completed";
    case iso_flash::WriteJobState::Failed: return "failed";
    case iso_flash::WriteJobState::DeviceLost: return "device-lost";
    case iso_flash::WriteJobState::Cancelled: return "cancelled";
    }
    return "unknown";
}

std::string write_finished_message(const iso_flash::WriteJobSnapshot& snapshot) {
    if (snapshot.state == iso_flash::WriteJobState::Completed) {
        return "Image written, synced, and verified by SHA-256 read-back";
    }
    if (snapshot.state == iso_flash::WriteJobState::DeviceLost) {
        return "Target device was lost; the device may contain a partial image";
    }
    if (snapshot.state == iso_flash::WriteJobState::Cancelled) {
        return snapshot.completed_bytes == 0
            ? "Write cancelled before any bytes were written"
            : "Write cancelled; the target may contain a partial image";
    }
    if (snapshot.state == iso_flash::WriteJobState::Failed) {
        return std::string("Write or verification failed: ") +
            g_strerror(snapshot.error_number) +
            (snapshot.completed_bytes == 0
                ? "" : "; the target may contain a partial image");
    }
    return "Write job finished";
}

gboolean publish_write_job_updates(gpointer user_data) {
    auto* connection = G_DBUS_CONNECTION(user_data);
    const auto jobs = g_write_jobs.snapshots();
    for (const auto& job : jobs) {
        const auto previous = g_last_emitted.find(job.id);
        const bool changed = previous == g_last_emitted.end() ||
            previous->second.state != job.snapshot.state ||
            previous->second.completed_bytes != job.snapshot.completed_bytes ||
            previous->second.total_bytes != job.snapshot.total_bytes ||
            previous->second.error_number != job.snapshot.error_number;

        if (iso_flash::is_terminal(job.snapshot.state)) {
            if (previous == g_last_emitted.end() ||
                !iso_flash::is_terminal(previous->second.state)) {
                GError* error = nullptr;
                (void)g_dbus_connection_emit_signal(
                    connection, job.owner.c_str(), kObjectPath,
                    "org.iso_flash.Manager1", "Finished",
                    g_variant_new("(tsttis)",
                        static_cast<guint64>(job.id),
                        write_state_name(job.snapshot.state),
                        static_cast<guint64>(job.snapshot.completed_bytes),
                        static_cast<guint64>(job.snapshot.total_bytes),
                        job.snapshot.error_number,
                        write_finished_message(job.snapshot).c_str()),
                    &error);
                if (error != nullptr) g_error_free(error);
            }
            g_last_emitted.erase(job.id);
        } else {
            if (changed) {
                GError* error = nullptr;
                (void)g_dbus_connection_emit_signal(
                    connection, job.owner.c_str(), kObjectPath,
                    "org.iso_flash.Manager1", "Progress",
                    g_variant_new("(tstt)",
                        static_cast<guint64>(job.id),
                        write_state_name(job.snapshot.state),
                        static_cast<guint64>(job.snapshot.completed_bytes),
                        static_cast<guint64>(job.snapshot.total_bytes)),
                    &error);
                if (error != nullptr) g_error_free(error);
            }
            g_last_emitted[job.id] = job.snapshot;
        }
    }

    (void)g_write_jobs.reap_terminal();
    return G_SOURCE_CONTINUE;
}

void job_owner_name_owner_changed(GDBusConnection*, const gchar*,
                                  const gchar*, const gchar*,
                                  const gchar*, GVariant* parameters,
                                  gpointer) {
    const gchar* name = nullptr;
    const gchar* old_owner = nullptr;
    const gchar* new_owner = nullptr;
    g_variant_get(parameters, "(&s&s&s)", &name, &old_owner, &new_owner);
    if (name != nullptr && name[0] == ':' &&
        old_owner != nullptr && old_owner[0] != '\0' &&
        new_owner != nullptr && new_owner[0] == '\0' &&
        g_strcmp0(name, old_owner) == 0) {
        (void)g_write_jobs.cancel_owner(name);
    }
}

void handle_method_call(GDBusConnection* connection, const gchar*, const gchar*,
                        const gchar*, const gchar* method_name,
                        GVariant* parameters, GDBusMethodInvocation* invocation,
                        gpointer) {
    if (g_strcmp0(method_name, "InspectTarget") == 0) {
        guint32 device_major = 0;
        guint32 device_minor = 0;
        g_variant_get(parameters, "(uu)", &device_major, &device_minor);

        if (device_major == 0) {
            return_inspection(invocation, false, 0,
                              "Invalid block-device major number");
            return;
        }

        const dev_t expected = makedev(device_major, device_minor);
        if (major(expected) != device_major || minor(expected) != device_minor) {
            return_inspection(invocation, false, 0,
                              "Block-device major/minor is out of range");
            return;
        }

        const std::string device = "/dev/block/" +
            std::to_string(device_major) + ":" + std::to_string(device_minor);
        std::string error;
        std::uint64_t capacity = 0;
        const bool accepted =
            inspect_target_read_only(device, error, capacity, expected);
        return_inspection(invocation, accepted, capacity,
                          accepted ? std::string{} : error);
        return;
    }

    if (g_strcmp0(method_name, "CheckWriteAuthorization") == 0) {
        const gchar* sender = g_dbus_method_invocation_get_sender(invocation);
        auto* task_data = new (std::nothrow) AuthorizationTaskData;
        if (task_data == nullptr || sender == nullptr || sender[0] != ':') {
            delete task_data;
            g_dbus_method_invocation_return_value(
                invocation,
                g_variant_new("(buuts)", FALSE, 0U, 0U,
                              static_cast<guint64>(0),
                              "Cannot identify D-Bus caller"));
            return;
        }
        task_data->connection = G_DBUS_CONNECTION(g_object_ref(connection));
        task_data->cancellable = g_cancellable_new();
        task_data->sender = sender;
        task_data->disconnect_subscription =
            iso_flash::subscribe_to_caller_disconnect(
                connection, task_data->sender.c_str(), task_data->cancellable);
        if (task_data->disconnect_subscription == 0) {
            delete task_data;
            g_dbus_method_invocation_return_value(
                invocation,
                g_variant_new("(buuts)", FALSE, 0U, 0U,
                              static_cast<guint64>(0),
                              "Cannot monitor D-Bus caller disconnect"));
            return;
        }
        GTask* task = g_task_new(
            nullptr, task_data->cancellable, authorization_complete,
            g_object_ref(invocation));
        g_task_set_task_data(
            task, task_data,
            [](gpointer data) { delete static_cast<AuthorizationTaskData*>(data); });
        g_task_run_in_thread(task, authorization_worker);
        g_object_unref(task);
        return;
    }

    if (g_strcmp0(method_name, "CancelWrite") == 0) {
        guint64 job_id = 0;
        g_variant_get(parameters, "(t)", &job_id);
        const gchar* sender = g_dbus_method_invocation_get_sender(invocation);
        const bool cancelled = sender != nullptr &&
            g_write_jobs.cancel(sender, static_cast<std::uint64_t>(job_id));
        g_dbus_method_invocation_return_value(
            invocation,
            g_variant_new("(bs)", cancelled,
                cancelled ? "" : "Unknown job or caller does not own this job"));
        return;
    }

    if (g_strcmp0(method_name, "StartWrite") == 0) {
#ifndef ISO_FLASH_ENABLE_WRITES
        g_dbus_method_invocation_return_value(
            invocation,
            g_variant_new("(bts)", FALSE, static_cast<guint64>(0),
                          "WRITE_DISABLED: write path is not enabled in this build"));
        return;
#endif
        gint32 handle = -1;
        guint32 device_major = 0;
        guint32 device_minor = 0;
        g_variant_get(parameters, "(huu)", &handle,
                      &device_major, &device_minor);
        if (device_major == 0) {
            g_dbus_method_invocation_return_value(
                invocation,
                g_variant_new("(bts)", FALSE, static_cast<guint64>(0),
                              "Invalid block-device major number"));
            return;
        }
        const dev_t expected_device = makedev(device_major, device_minor);
        if (major(expected_device) != device_major ||
            minor(expected_device) != device_minor) {
            g_dbus_method_invocation_return_value(
                invocation,
                g_variant_new("(bts)", FALSE, static_cast<guint64>(0),
                              "Block-device major/minor is out of range"));
            return;
        }

        GDBusMessage* message = g_dbus_method_invocation_get_message(invocation);
        GUnixFDList* fd_list =
            message != nullptr ? g_dbus_message_get_unix_fd_list(message) : nullptr;
        if (fd_list == nullptr) {
            g_dbus_method_invocation_return_value(
                invocation,
                g_variant_new("(bts)", FALSE, static_cast<guint64>(0),
                              "D-Bus request did not include a Unix file descriptor list"));
            return;
        }

        GError* error = nullptr;
        const int received_fd = g_unix_fd_list_get(fd_list, handle, &error);
        if (received_fd < 0) {
            const std::string reason = error != nullptr
                ? error->message : "Cannot receive ISO file descriptor";
            if (error != nullptr) g_error_free(error);
            g_dbus_method_invocation_return_value(
                invocation,
                g_variant_new("(bts)", FALSE, static_cast<guint64>(0),
                              reason.c_str()));
            return;
        }

        auto capacity_lease = g_image_validation_limiter.try_acquire();
        if (!capacity_lease) {
            (void)::close(received_fd);
            g_dbus_method_invocation_return_value(
                invocation,
                g_variant_new("(bts)", FALSE, static_cast<guint64>(0),
                              "Image validation capacity is busy; retry later"));
            return;
        }

        auto* task_data = new (std::nothrow) StartWriteTaskData;
        if (task_data == nullptr) {
            (void)::close(received_fd);
            g_dbus_method_invocation_return_value(
                invocation,
                g_variant_new("(bts)", FALSE, static_cast<guint64>(0),
                              "Cannot allocate StartWrite task"));
            return;
        }
        task_data->descriptor.reset(received_fd);
        task_data->capacity_lease = std::move(capacity_lease);
        task_data->connection = G_DBUS_CONNECTION(g_object_ref(connection));
        task_data->cancellable = g_cancellable_new();
        task_data->sender = g_dbus_method_invocation_get_sender(invocation) != nullptr
            ? g_dbus_method_invocation_get_sender(invocation) : "";
        task_data->expected_device = expected_device;
        if (task_data->sender.empty() || task_data->sender.front() != ':' ||
            task_data->cancellable == nullptr) {
            delete task_data;
            g_dbus_method_invocation_return_value(
                invocation,
                g_variant_new("(bts)", FALSE, static_cast<guint64>(0),
                              "Cannot identify or monitor the D-Bus caller"));
            return;
        }

        task_data->disconnect_subscription =
            iso_flash::subscribe_to_caller_disconnect(
                connection, task_data->sender.c_str(),
                task_data->cancellable);
        if (task_data->disconnect_subscription == 0) {
            delete task_data;
            g_dbus_method_invocation_return_value(
                invocation,
                g_variant_new("(bts)", FALSE, static_cast<guint64>(0),
                              "Cannot monitor D-Bus caller disconnect"));
            return;
        }

        GTask* task = g_task_new(
            nullptr, task_data->cancellable, start_write_complete,
            g_object_ref(invocation));
        g_task_set_task_data(
            task, task_data,
            [](gpointer data) {
                delete static_cast<StartWriteTaskData*>(data);
            });
        g_task_run_in_thread(task, start_write_worker);
        g_object_unref(task);
        return;
    }

    if (g_strcmp0(method_name, "ValidateImage") == 0) {
        gint32 handle = -1;
        g_variant_get(parameters, "(h)", &handle);
        GDBusMessage* message = g_dbus_method_invocation_get_message(invocation);
        GUnixFDList* fd_list =
            message != nullptr ? g_dbus_message_get_unix_fd_list(message) : nullptr;
        if (fd_list == nullptr) {
            g_dbus_method_invocation_return_value(
                invocation,
                g_variant_new("(btss)", FALSE, static_cast<guint64>(0), "",
                              "D-Bus request did not include a Unix file descriptor list"));
            return;
        }

        GError* error = nullptr;
        const int received_fd = g_unix_fd_list_get(fd_list, handle, &error);
        if (received_fd < 0) {
            const std::string reason = error != nullptr
                ? error->message : "Cannot receive image file descriptor";
            if (error != nullptr) g_error_free(error);
            g_dbus_method_invocation_return_value(
                invocation,
                g_variant_new("(btss)", FALSE, static_cast<guint64>(0), "",
                              reason.c_str()));
            return;
        }

        auto capacity_lease = g_image_validation_limiter.try_acquire();
        if (!capacity_lease) {
            (void)::close(received_fd);
            g_dbus_method_invocation_return_value(
                invocation,
                g_variant_new("(btss)", FALSE, static_cast<guint64>(0), "",
                              "Image validation capacity is busy; retry later"));
            return;
        }

        auto* task_data = new (std::nothrow) ImageValidationTaskData;
        if (task_data == nullptr) {
            (void)::close(received_fd);
            g_dbus_method_invocation_return_value(
                invocation,
                g_variant_new("(btss)", FALSE, static_cast<guint64>(0), "",
                              "Cannot allocate image validation task"));
            return;
        }
        task_data->descriptor.reset(received_fd);
        task_data->capacity_lease = std::move(capacity_lease);
        task_data->cancellable = g_cancellable_new();
        task_data->connection = G_DBUS_CONNECTION(g_object_ref(connection));
        const gchar* sender = g_dbus_method_invocation_get_sender(invocation);
        task_data->sender = sender != nullptr ? sender : "";
        if (task_data->sender.empty()) {
            delete task_data;
            g_dbus_method_invocation_return_value(
                invocation,
                g_variant_new("(btss)", FALSE, static_cast<guint64>(0), "",
                              "Cannot identify the D-Bus caller for cancellation tracking"));
            return;
        }

        task_data->disconnect_subscription =
            iso_flash::subscribe_to_caller_disconnect(
                connection, task_data->sender.c_str(),
                task_data->cancellable);
        if (task_data->disconnect_subscription == 0) {
            delete task_data;
            g_dbus_method_invocation_return_value(
                invocation,
                g_variant_new("(btss)", FALSE, static_cast<guint64>(0), "",
                              "Cannot monitor D-Bus caller disconnect"));
            return;
        }

        GTask* task = g_task_new(
            nullptr, task_data->cancellable, image_validation_complete,
            g_object_ref(invocation));
        g_task_set_task_data(
            task, task_data,
            [](gpointer data) {
                delete static_cast<ImageValidationTaskData*>(data);
            });
        g_task_run_in_thread(task, image_validation_worker);
        g_object_unref(task);
        return;
    }

    g_dbus_method_invocation_return_error(
        invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
        "Method is not implemented");
}

const GDBusInterfaceVTable kVTable = {
    handle_method_call,
    nullptr,
    nullptr,
    {nullptr}
};
}  // namespace

int main() {
    GError* error = nullptr;
    GDBusNodeInfo* introspection =
        g_dbus_node_info_new_for_xml(kIntrospectionXml, &error);
    if (introspection == nullptr) {
        g_printerr("Cannot parse D-Bus interface: %s\n",
                   error != nullptr ? error->message : "unknown error");
        if (error != nullptr) g_error_free(error);
        return 1;
    }

    GDBusConnection* connection =
        g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
    if (connection == nullptr) {
        g_printerr("Cannot connect to system D-Bus: %s\n",
                   error != nullptr ? error->message : "unknown error");
        if (error != nullptr) g_error_free(error);
        g_dbus_node_info_unref(introspection);
        return 1;
    }

    const guint registration_id = g_dbus_connection_register_object(
        connection, kObjectPath, introspection->interfaces[0], &kVTable,
        nullptr, nullptr, &error);
    if (registration_id == 0) {
        g_printerr("Cannot export D-Bus interface: %s\n",
                   error != nullptr ? error->message : "unknown error");
        if (error != nullptr) g_error_free(error);
        g_object_unref(connection);
        g_dbus_node_info_unref(introspection);
        return 1;
    }

    const guint owner_id = g_bus_own_name_on_connection(
        connection, kBusName, G_BUS_NAME_OWNER_FLAGS_NONE, nullptr, nullptr,
        nullptr, nullptr);
    if (owner_id == 0) {
        g_printerr("Cannot own D-Bus service name\n");
        g_dbus_connection_unregister_object(connection, registration_id);
        g_object_unref(connection);
        g_dbus_node_info_unref(introspection);
        return 1;
    }

    const guint owner_watch_subscription =
        g_dbus_connection_signal_subscribe(
            connection, "org.freedesktop.DBus", "org.freedesktop.DBus",
            "NameOwnerChanged", "/org/freedesktop/DBus", nullptr,
            G_DBUS_SIGNAL_FLAGS_NONE, job_owner_name_owner_changed,
            nullptr, nullptr);
    if (owner_watch_subscription == 0) {
        g_printerr("Cannot monitor D-Bus owners for write-job cancellation\\n");
        g_bus_unown_name(owner_id);
        g_dbus_connection_unregister_object(connection, registration_id);
        g_object_unref(connection);
        g_dbus_node_info_unref(introspection);
        return 1;
    }

    const guint update_timer = g_timeout_add(
        100, publish_write_job_updates, connection);
    GMainLoop* loop = g_main_loop_new(nullptr, FALSE);
    g_main_loop_run(loop);

    g_main_loop_unref(loop);
    if (update_timer != 0) g_source_remove(update_timer);
    g_dbus_connection_signal_unsubscribe(
        connection, owner_watch_subscription);
    g_write_jobs.shutdown();
    g_last_emitted.clear();
    g_bus_unown_name(owner_id);
    g_dbus_connection_unregister_object(connection, registration_id);
    g_object_unref(connection);
    g_dbus_node_info_unref(introspection);
    return 0;
}
