#include "dbus_caller_watch.hpp"
#include "device_guard.hpp"
#include "device_io.hpp"
#include "image_fd_validation.hpp"
#include "image_validation_limiter.hpp"
#include "image_validation_task.hpp"

#include <gio/gio.h>
#include <gio/gunixfdlist.h>

#include <cstdint>
#include <new>
#include <string>
#include <utility>
#include <sys/sysmacros.h>
#include <unistd.h>

namespace {
constexpr const char* kBusName = "org.iso_flash.Manager1";
constexpr const char* kObjectPath = "/org/iso_flash/Manager1";
iso_flash::ImageValidationLimiter g_image_validation_limiter(2);

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
    <method name="ValidateImage">
      <arg name="image_fd" type="h" direction="in"/>
      <arg name="accepted" type="b" direction="out"/>
      <arg name="size_bytes" type="t" direction="out"/>
      <arg name="sha256" type="s" direction="out"/>
      <arg name="reason" type="s" direction="out"/>
    </method>
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

    GMainLoop* loop = g_main_loop_new(nullptr, FALSE);
    g_main_loop_run(loop);

    g_main_loop_unref(loop);
    g_bus_unown_name(owner_id);
    g_dbus_connection_unregister_object(connection, registration_id);
    g_object_unref(connection);
    g_dbus_node_info_unref(introspection);
    return 0;
}
