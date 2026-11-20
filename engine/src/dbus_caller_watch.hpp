#pragma once

#include <gio/gio.h>

namespace iso_flash {

inline void caller_name_owner_changed(GDBusConnection*, const gchar*,
                                      const gchar*, const gchar*,
                                      const gchar*, GVariant* parameters,
                                      gpointer user_data) {
    auto* cancellable = G_CANCELLABLE(user_data);
    const gchar* name = nullptr;
    const gchar* old_owner = nullptr;
    const gchar* new_owner = nullptr;
    g_variant_get(parameters, "(&s&s&s)", &name, &old_owner, &new_owner);

    if (name != nullptr && old_owner != nullptr && new_owner != nullptr &&
        g_strcmp0(name, old_owner) == 0 && new_owner[0] == '\0') {
        g_cancellable_cancel(cancellable);
    }
}

inline guint subscribe_to_caller_disconnect(GDBusConnection* connection,
                                            const gchar* unique_name,
                                            GCancellable* cancellable) {
    if (connection == nullptr || unique_name == nullptr ||
        unique_name[0] != ':' || cancellable == nullptr) {
        return 0;
    }

    return g_dbus_connection_signal_subscribe(
        connection, "org.freedesktop.DBus", "org.freedesktop.DBus",
        "NameOwnerChanged", "/org/freedesktop/DBus", unique_name,
        G_DBUS_SIGNAL_FLAGS_NONE, caller_name_owner_changed,
        g_object_ref(cancellable),
        [](gpointer data) { g_object_unref(data); });
}

}  // namespace iso_flash
