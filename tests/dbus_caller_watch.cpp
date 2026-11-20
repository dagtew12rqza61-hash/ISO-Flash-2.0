#include "dbus_caller_watch.hpp"

#include <iostream>

namespace {

GDBusConnection* connect_to_test_bus() {
    const gchar* address = g_getenv("DBUS_SYSTEM_BUS_ADDRESS");
    if (address == nullptr || address[0] == '\0') {
        std::cerr << "DBUS_SYSTEM_BUS_ADDRESS is not set\n";
        return nullptr;
    }

    GError* error = nullptr;
    auto* connection = g_dbus_connection_new_for_address_sync(
        address,
        static_cast<GDBusConnectionFlags>(
            G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
            G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
        nullptr, nullptr, &error);
    if (connection == nullptr) {
        std::cerr << (error != nullptr ? error->message : "D-Bus connection failed")
                  << '\n';
        if (error != nullptr) g_error_free(error);
    }
    return connection;
}

bool wait_for_cancellation(GCancellable* cancellable) {
    const gint64 deadline = g_get_monotonic_time() + 2 * G_TIME_SPAN_SECOND;
    while (!g_cancellable_is_cancelled(cancellable) &&
           g_get_monotonic_time() < deadline) {
        while (g_main_context_iteration(nullptr, FALSE)) {
        }
        g_usleep(1000);
    }
    return g_cancellable_is_cancelled(cancellable);
}

}  // namespace

int main() {
    auto* observer = connect_to_test_bus();
    auto* caller = connect_to_test_bus();
    auto* unrelated = connect_to_test_bus();
    if (observer == nullptr || caller == nullptr || unrelated == nullptr) {
        if (observer != nullptr) g_object_unref(observer);
        if (caller != nullptr) g_object_unref(caller);
        if (unrelated != nullptr) g_object_unref(unrelated);
        return 1;
    }

    const gchar* caller_name = g_dbus_connection_get_unique_name(caller);
    auto* cancellable = g_cancellable_new();
    const guint subscription = iso_flash::subscribe_to_caller_disconnect(
        observer, caller_name, cancellable);
    if (subscription == 0) {
        std::cerr << "Could not subscribe to caller disconnect signal\n";
        g_object_unref(cancellable);
        g_object_unref(unrelated);
        g_object_unref(caller);
        g_object_unref(observer);
        return 1;
    }

    GError* error = nullptr;
    const gboolean unrelated_closed =
        g_dbus_connection_close_sync(unrelated, nullptr, &error);
    if (!unrelated_closed) {
        std::cerr << (error != nullptr ? error->message : "Could not close unrelated client")
                  << '\n';
        if (error != nullptr) g_error_free(error);
        g_dbus_connection_signal_unsubscribe(observer, subscription);
        g_object_unref(cancellable);
        g_object_unref(unrelated);
        g_object_unref(caller);
        g_object_unref(observer);
        return 1;
    }

    const gint64 unrelated_deadline = g_get_monotonic_time() + 200 * 1000;
    while (g_get_monotonic_time() < unrelated_deadline) {
        while (g_main_context_iteration(nullptr, FALSE)) {
        }
        g_usleep(1000);
    }
    if (g_cancellable_is_cancelled(cancellable)) {
        std::cerr << "Unrelated client disconnect cancelled the task\n";
        g_dbus_connection_signal_unsubscribe(observer, subscription);
        g_object_unref(cancellable);
        g_object_unref(unrelated);
        g_object_unref(caller);
        g_object_unref(observer);
        return 1;
    }

    const gboolean caller_closed =
        g_dbus_connection_close_sync(caller, nullptr, &error);
    if (!caller_closed) {
        std::cerr << (error != nullptr ? error->message : "Could not close caller")
                  << '\n';
        if (error != nullptr) g_error_free(error);
    }

    const bool cancelled = caller_closed && wait_for_cancellation(cancellable);
    g_dbus_connection_signal_unsubscribe(observer, subscription);
    g_object_unref(cancellable);
    g_object_unref(unrelated);
    g_object_unref(caller);
    g_object_unref(observer);

    if (!cancelled) {
        std::cerr << "Caller disconnect did not cancel the task\n";
        return 1;
    }
    return 0;
}
