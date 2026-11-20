#include <gio/gio.h>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

bool call_u32(GDBusConnection* connection, const char* method,
              const char* name, guint32& value) {
    GError* error = nullptr;
    GVariant* reply = g_dbus_connection_call_sync(
        connection, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", method, g_variant_new("(s)", name),
        G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &error);
    if (reply == nullptr) {
        std::cerr << (error != nullptr ? error->message : "D-Bus identity query failed") << '\n';
        if (error != nullptr) g_error_free(error);
        return false;
    }
    g_variant_get(reply, "(u)", &value);
    g_variant_unref(reply);
    return true;
}

bool verify_real_polkit_owner(GDBusConnection* connection) {
    GError* error = nullptr;
    GVariant* owner_reply = g_dbus_connection_call_sync(
        connection, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "GetNameOwner",
        g_variant_new("(s)", "org.freedesktop.PolicyKit1"),
        G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &error);
    if (owner_reply == nullptr) {
        std::cerr << (error != nullptr ? error->message : "Polkit has no D-Bus owner") << '\n';
        if (error != nullptr) g_error_free(error);
        return false;
    }
    const gchar* owner = nullptr;
    g_variant_get(owner_reply, "(&s)", &owner);
    const std::string unique_owner = owner != nullptr ? owner : "";
    g_variant_unref(owner_reply);

    guint32 pid = 0;
    if (unique_owner.empty() ||
        !call_u32(connection, "GetConnectionUnixProcessID",
                  unique_owner.c_str(), pid) || pid == 0) {
        std::cerr << "Cannot identify the real Polkit daemon process\n";
        return false;
    }

    std::ifstream comm("/proc/" + std::to_string(pid) + "/comm");
    std::string process_name;
    std::getline(comm, process_name);
    if (process_name != "polkitd") {
        std::cerr << "org.freedesktop.PolicyKit1 is not owned by polkitd: "
                  << process_name << '\n';
        return false;
    }
    return true;
}

}  // namespace

int main() {
    if (::getuid() == 0) {
        std::cerr << "Real Polkit test must run as an unprivileged caller\n";
        return 1;
    }

    GError* error = nullptr;
    GDBusConnection* connection =
        g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
    if (connection == nullptr) {
        std::cerr << (error != nullptr ? error->message : "Cannot connect to system D-Bus") << '\n';
        if (error != nullptr) g_error_free(error);
        return 2;
    }

    bool ok = verify_real_polkit_owner(connection);
    GVariant* reply = g_dbus_connection_call_sync(
        connection, "org.iso_flash.Manager1", "/org/iso_flash/Manager1",
        "org.iso_flash.Manager1", "CheckWriteAuthorization",
        nullptr, G_VARIANT_TYPE("(buuts)"), G_DBUS_CALL_FLAGS_NONE,
        30000, nullptr, &error);
    if (reply == nullptr) {
        std::cerr << (error != nullptr ? error->message :
                      "Real Polkit authorization call failed") << '\n';
        if (error != nullptr) g_error_free(error);
        g_object_unref(connection);
        return 3;
    }

    gboolean authorized = FALSE;
    guint32 uid = 0;
    guint32 pid = 0;
    guint64 start_ticks = 0;
    const gchar* reason = nullptr;
    g_variant_get(reply, "(buut&s)", &authorized, &uid, &pid,
                  &start_ticks, &reason);
    if (authorized || uid != static_cast<guint32>(::getuid()) ||
        pid != static_cast<guint32>(::getpid()) || start_ticks == 0 ||
        reason == nullptr || reason[0] == '\0') {
        std::cerr << "Real Polkit did not fail closed as expected: authorized="
                  << authorized << " uid=" << uid << " pid=" << pid
                  << " start=" << start_ticks << " reason="
                  << (reason != nullptr ? reason : "(null)") << '\n';
        ok = false;
    } else {
        std::cout << "Real system Polkit denied/challenged unprivileged write request: "
                  << reason << '\n';
    }

    g_variant_unref(reply);
    g_object_unref(connection);
    return ok ? 0 : 4;
}
