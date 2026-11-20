#pragma once

#include <gio/gio.h>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace iso_flash {

struct DbusCallerIdentity {
    guint32 uid = 0;
    guint32 pid = 0;
    std::uint64_t process_start_ticks = 0;
};

struct PolkitAuthorizationResult {
    bool authorized = false;
    bool challenge = false;
    DbusCallerIdentity caller;
    std::string reason;
};

inline bool read_process_start_ticks(guint32 pid, std::uint64_t& ticks) {
    if (pid == 0) return false;
    std::ifstream input("/proc/" + std::to_string(pid) + "/stat");
    std::string line;
    if (!std::getline(input, line)) return false;
    const auto closing = line.rfind(')');
    if (closing == std::string::npos || closing + 2 >= line.size()) return false;
    std::istringstream fields(line.substr(closing + 2));
    std::string field;
    for (unsigned index = 0; index <= 19; ++index) {
        if (!(fields >> field)) return false;
        if (index == 19) {
            try {
                std::size_t consumed = 0;
                const auto value = std::stoull(field, &consumed, 10);
                if (consumed != field.size() || value == 0) return false;
                ticks = value;
                return true;
            } catch (...) {
                return false;
            }
        }
    }
    return false;
}

inline bool read_process_real_uid(guint32 pid, guint32& uid) {
    if (pid == 0) return false;
    std::ifstream input("/proc/" + std::to_string(pid) + "/status");
    std::string line;
    while (std::getline(input, line)) {
        if (line.rfind("Uid:", 0) != 0) continue;
        std::istringstream values(line.substr(4));
        std::uint64_t real_uid = 0;
        if (!(values >> real_uid) || real_uid > G_MAXUINT32) return false;
        uid = static_cast<guint32>(real_uid);
        return true;
    }
    return false;
}

inline bool query_dbus_caller_identity(GDBusConnection* connection,
                                       const gchar* sender,
                                       DbusCallerIdentity& identity,
                                       GCancellable* cancellable,
                                       GError** error) {
    if (connection == nullptr || sender == nullptr || sender[0] != ':') {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "Missing unique D-Bus caller identity");
        return false;
    }

    GVariant* pid_reply = g_dbus_connection_call_sync(
        connection, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "GetConnectionUnixProcessID",
        g_variant_new("(s)", sender), G_VARIANT_TYPE("(u)"),
        G_DBUS_CALL_FLAGS_NONE, 5000, cancellable, error);
    if (pid_reply == nullptr) return false;
    guint32 pid = 0;
    g_variant_get(pid_reply, "(u)", &pid);
    g_variant_unref(pid_reply);

    GVariant* uid_reply = g_dbus_connection_call_sync(
        connection, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "GetConnectionUnixUser",
        g_variant_new("(s)", sender), G_VARIANT_TYPE("(u)"),
        G_DBUS_CALL_FLAGS_NONE, 5000, cancellable, error);
    if (uid_reply == nullptr) return false;
    guint32 uid = 0;
    g_variant_get(uid_reply, "(u)", &uid);
    g_variant_unref(uid_reply);

    guint32 proc_uid = 0;
    std::uint64_t start_ticks = 0;
    if (!read_process_real_uid(pid, proc_uid) || proc_uid != uid ||
        !read_process_start_ticks(pid, start_ticks)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                            "D-Bus credentials do not match a live /proc process identity");
        return false;
    }
    identity = {uid, pid, start_ticks};
    return true;
}

inline PolkitAuthorizationResult authorize_dbus_sender(
    GDBusConnection* connection, const gchar* sender,
    GCancellable* cancellable = nullptr) {
    PolkitAuthorizationResult result;
    GError* error = nullptr;
    if (!query_dbus_caller_identity(connection, sender, result.caller,
                                    cancellable, &error)) {
        result.reason = error != nullptr ? error->message :
            "Cannot obtain D-Bus caller credentials";
        if (error != nullptr) g_error_free(error);
        return result;
    }

    GVariantBuilder subject_builder;
    g_variant_builder_init(&subject_builder, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&subject_builder, "{sv}", "name",
                          g_variant_new_string(sender));
    GVariant* subject = g_variant_new(
        "(s@a{sv})", "system-bus-name",
        g_variant_builder_end(&subject_builder));
    GVariantBuilder details_builder;
    g_variant_builder_init(&details_builder, G_VARIANT_TYPE("a{ss}"));
    GVariant* parameters = g_variant_new(
        "(@(sa{sv})s@a{ss}us)", subject, "org.iso_flash.write",
        g_variant_builder_end(&details_builder), 1U, "");

    GVariant* reply = g_dbus_connection_call_sync(
        connection, "org.freedesktop.PolicyKit1",
        "/org/freedesktop/PolicyKit1/Authority",
        "org.freedesktop.PolicyKit1.Authority", "CheckAuthorization",
        parameters, G_VARIANT_TYPE("((bba{ss}))"),
        G_DBUS_CALL_FLAGS_NONE, 120000, cancellable, &error);
    if (reply == nullptr) {
        result.reason = error != nullptr ? error->message :
            "Polkit authorization service is unavailable";
        if (error != nullptr) g_error_free(error);
        return result;
    }

    GVariant* authorization_result = nullptr;
    g_variant_get(reply, "(@(bba{ss}))", &authorization_result);
    g_variant_unref(reply);
    if (authorization_result == nullptr) {
        result.reason = "Polkit returned an empty authorization result";
        return result;
    }

    gboolean authorized = FALSE;
    gboolean challenge = FALSE;
    GVariant* returned_details = nullptr;
    g_variant_get(authorization_result, "(bb@a{ss})",
                  &authorized, &challenge, &returned_details);
    g_variant_unref(returned_details);
    g_variant_unref(authorization_result);

    DbusCallerIdentity after;
    if (!query_dbus_caller_identity(connection, sender, after,
                                    cancellable, &error) ||
        after.uid != result.caller.uid ||
        after.pid != result.caller.pid ||
        after.process_start_ticks != result.caller.process_start_ticks) {
        result.reason = error != nullptr ? error->message :
            "D-Bus caller process identity changed during authorization";
        if (error != nullptr) g_error_free(error);
        return result;
    }

    result.authorized = authorized != FALSE;
    result.challenge = challenge != FALSE;
    if (!result.authorized) {
        result.reason = result.challenge
            ? "Polkit requires interactive administrator authentication"
            : "Polkit denied the requested write operation";
    }
    return result;
}

}  // namespace iso_flash
