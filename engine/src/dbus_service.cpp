#include "device_guard.hpp"

#include <gio/gio.h>

#include <exception>
#include <string>
#include <sys/sysmacros.h>

namespace {
constexpr const char* kBusName = "org.iso_flash.Manager1";
constexpr const char* kObjectPath = "/org/iso_flash/Manager1";
constexpr const char* kInterfaceName = "org.iso_flash.Manager1";

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
  </interface>
</node>
)xml";

void handle_method_call(GDBusConnection*, const gchar*, const gchar*,
                        const gchar*, const gchar* method_name,
                        GVariant* parameters, GDBusMethodInvocation* invocation,
                        gpointer) {
    if (g_strcmp0(method_name, "InspectTarget") != 0) {
        g_dbus_method_invocation_return_error(
            invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
            "Method is not implemented");
        return;
    }

    guint32 device_major = 0;
    guint32 device_minor = 0;
    g_variant_get(parameters, "(uu)", &device_major, &device_minor);

    if (device_major == 0) {
        g_dbus_method_invocation_return_value(
            invocation, g_variant_new("(bts)", FALSE, static_cast<guint64>(0),
                                      "Invalid block-device major number"));
        return;
    }

    const std::string device = "/dev/block/" + std::to_string(device_major) +
                               ":" + std::to_string(device_minor);
    std::string error;
    std::uint64_t capacity_bytes = 0;
    const dev_t expected_device_number = makedev(device_major, device_minor);
    if (major(expected_device_number) != device_major ||
        minor(expected_device_number) != device_minor) {
        g_dbus_method_invocation_return_value(
            invocation, g_variant_new("(bts)", FALSE, static_cast<guint64>(0),
                                      "Block-device major/minor is out of range"));
        return;
    }
    const bool accepted =
        inspect_target_read_only(device, error, capacity_bytes, expected_device_number);

    g_dbus_method_invocation_return_value(
        invocation,
        g_variant_new("(bts)", accepted,
                      static_cast<guint64>(capacity_bytes),
                      accepted ? "" : error.c_str()));
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
