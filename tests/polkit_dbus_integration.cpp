#include <gio/gio.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>

namespace {

constexpr const char* kXml = R"xml(
<node>
  <interface name="org.freedesktop.PolicyKit1.Authority">
    <method name="CheckAuthorization">
      <arg type="(sa{sv})" direction="in"/>
      <arg type="s" direction="in"/>
      <arg type="a{ss}" direction="in"/>
      <arg type="u" direction="in"/>
      <arg type="s" direction="in"/>
      <arg type="b" direction="out"/>
      <arg type="b" direction="out"/>
      <arg type="a{ss}" direction="out"/>
    </method>
  </interface>
</node>
)xml";

std::atomic<int> g_mode{1};
std::atomic<unsigned> g_calls{0};
std::mutex g_mutex;
std::condition_variable g_ready_condition;
bool g_ready = false;
bool g_failed = false;
GMainLoop* g_loop = nullptr;

void handle_authority_call(GDBusConnection*, const gchar*, const gchar*,
                           const gchar*, const gchar* method,
                           GVariant* parameters,
                           GDBusMethodInvocation* invocation, gpointer) {
    if (g_strcmp0(method, "CheckAuthorization") != 0) {
        g_dbus_method_invocation_return_error(
            invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
            "Unexpected PolicyKit method");
        return;
    }

    GVariant* subject = nullptr;
    const gchar* action = nullptr;
    GVariant* details = nullptr;
    guint32 flags = 0;
    const gchar* cancellation_id = nullptr;
    g_variant_get(parameters, "(@(sa{sv})&s@a{ss}u&s)",
                  &subject, &action, &details, &flags, &cancellation_id);
    const gchar* subject_kind = nullptr;
    GVariant* subject_details = nullptr;
    g_variant_get(subject, "(&s@a{sv})", &subject_kind, &subject_details);
    const gchar* sender = nullptr;
    const bool valid_subject =
        g_strcmp0(subject_kind, "system-bus-name") == 0 &&
        g_variant_lookup(subject_details, "name", "&s", &sender) &&
        sender != nullptr && sender[0] == ':' &&
        g_strcmp0(action, "org.iso_flash.write") == 0 &&
        flags == 1U && cancellation_id != nullptr &&
        cancellation_id[0] == '\0';
    g_variant_unref(subject_details);
    g_variant_unref(subject);
    g_variant_unref(details);
    if (!valid_subject) {
        g_dbus_method_invocation_return_error(
            invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
            "Invalid Polkit subject or action parameters");
        return;
    }

    ++g_calls;
    const int mode = g_mode.load();
    if (mode < 0) {
        g_dbus_method_invocation_return_error(
            invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
            "Synthetic Polkit authority failure");
        return;
    }

    GVariantBuilder result_details;
    g_variant_builder_init(&result_details, G_VARIANT_TYPE("a{ss}"));
    g_dbus_method_invocation_return_value(
        invocation,
        g_variant_new("(bb@a{ss})", mode != 0, FALSE,
                      g_variant_builder_end(&result_details)));
}

void authority_name_lost(GDBusConnection*, const gchar*, gpointer) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_failed = true;
    g_ready = true;
    g_ready_condition.notify_all();
    std::cerr << "Synthetic Polkit authority could not own its D-Bus name\\n";
}

void authority_name_acquired(GDBusConnection*, const gchar*, gpointer) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_ready = true;
    g_ready_condition.notify_all();
}

void authority_thread() {
    GMainContext* context = g_main_context_new();
    g_main_context_push_thread_default(context);
    const gchar* address = g_getenv("DBUS_SYSTEM_BUS_ADDRESS");
    GError* error = nullptr;
    GDBusConnection* connection = address != nullptr
        ? g_dbus_connection_new_for_address_sync(
              address,
              static_cast<GDBusConnectionFlags>(
                  G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                  G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
              nullptr, nullptr, &error)
        : nullptr;
    if (connection == nullptr) {
        std::cerr << "Cannot connect synthetic Polkit authority to test bus: "
                  << (error != nullptr ? error->message : "unknown D-Bus error") << '\\n';
        std::lock_guard<std::mutex> lock(g_mutex);
        g_failed = true;
        g_ready = true;
        g_ready_condition.notify_all();
        if (error != nullptr) g_error_free(error);
        g_main_context_pop_thread_default(context);
        g_main_context_unref(context);
        return;
    }

    GDBusNodeInfo* node = g_dbus_node_info_new_for_xml(kXml, &error);
    const GDBusInterfaceVTable vtable = {
        handle_authority_call, nullptr, nullptr, {nullptr}
    };
    const guint registration = node != nullptr
        ? g_dbus_connection_register_object(
              connection, "/org/freedesktop/PolicyKit1/Authority",
              node->interfaces[0], &vtable, nullptr, nullptr, &error)
        : 0;
    if (registration == 0) {
        std::cerr << "Cannot register synthetic Polkit authority: "
                  << (error != nullptr ? error->message : "unknown introspection error") << '\\n';
        std::lock_guard<std::mutex> lock(g_mutex);
        g_failed = true;
        g_ready = true;
        g_ready_condition.notify_all();
        if (error != nullptr) g_error_free(error);
        if (node != nullptr) g_dbus_node_info_unref(node);
        g_object_unref(connection);
        g_main_context_pop_thread_default(context);
        g_main_context_unref(context);
        return;
    }

    g_loop = g_main_loop_new(context, FALSE);
    guint owner = g_bus_own_name_on_connection(
        connection, "org.freedesktop.PolicyKit1",
        G_BUS_NAME_OWNER_FLAGS_NONE, nullptr, authority_name_acquired,
        authority_name_lost, nullptr);
    g_main_loop_run(g_loop);
    g_bus_unown_name(owner);
    g_dbus_connection_unregister_object(connection, registration);
    g_main_loop_unref(g_loop);
    g_loop = nullptr;
    g_dbus_node_info_unref(node);
    g_object_unref(connection);
    g_main_context_pop_thread_default(context);
    g_main_context_unref(context);
}

bool call_authorization(GDBusConnection* connection, bool expected,
                        bool expect_reason, bool verify_identity) {
    GError* error = nullptr;
    GVariant* reply = g_dbus_connection_call_sync(
        connection, "org.iso_flash.Manager1", "/org/iso_flash/Manager1",
        "org.iso_flash.Manager1", "CheckWriteAuthorization",
        nullptr, G_VARIANT_TYPE("(buuts)"), G_DBUS_CALL_FLAGS_NONE,
        15000, nullptr, &error);
    if (reply == nullptr) {
        std::cerr << (error != nullptr ? error->message :
                      "CheckWriteAuthorization call failed") << '\n';
        if (error != nullptr) g_error_free(error);
        return false;
    }
    gboolean authorized = FALSE;
    guint32 uid = 0;
    guint32 pid = 0;
    guint64 start_ticks = 0;
    const gchar* reason = nullptr;
    g_variant_get(reply, "(buut&s)", &authorized, &uid, &pid,
                  &start_ticks, &reason);
    const bool identity_ok = !verify_identity ||
        (uid == static_cast<guint32>(::getuid()) &&
         pid == static_cast<guint32>(::getpid()) && start_ticks != 0);
    const bool ok = (authorized != FALSE) == expected &&
        identity_ok && (expect_reason ? reason[0] != '\0' : reason[0] == '\0');
    if (!ok) {
        std::cerr << "Unexpected Polkit result: authorized="
                  << authorized << " uid=" << uid << " pid=" << pid
                  << " start=" << start_ticks << " reason=" << reason << '\n';
    }
    g_variant_unref(reply);
    return ok;
}

}  // namespace

int main() {
    std::thread authority(authority_thread);
    {
        std::unique_lock<std::mutex> lock(g_mutex);
        if (!g_ready_condition.wait_for(
                lock, std::chrono::seconds(5), [] { return g_ready; }) ||
            g_failed) {
            std::cerr << "Synthetic Polkit authority failed to start\n";
            lock.unlock();
            if (g_loop != nullptr) g_main_loop_quit(g_loop);
            authority.join();
            return 1;
        }
    }

    GError* error = nullptr;
    GDBusConnection* client =
        g_dbus_connection_new_for_address_sync(
            g_getenv("DBUS_SYSTEM_BUS_ADDRESS"),
            static_cast<GDBusConnectionFlags>(
                G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
            nullptr, nullptr, &error);
    if (client == nullptr) {
        std::cerr << (error != nullptr ? error->message :
                      "Cannot connect integration client") << '\n';
        if (error != nullptr) g_error_free(error);
        g_main_loop_quit(g_loop);
        authority.join();
        return 2;
    }

    bool ok = true;
    g_mode = 1;
    ok = call_authorization(client, true, false, true) && ok;
    g_mode = 0;
    ok = call_authorization(client, false, true, true) && ok;
    g_mode = -1;
    ok = call_authorization(client, false, true, true) && ok;
    if (g_calls.load() != 3) {
        std::cerr << "Polkit authority did not receive all expected calls\n";
        ok = false;
    }

    g_dbus_connection_close_sync(client, nullptr, nullptr);
    g_object_unref(client);
    g_main_loop_quit(g_loop);
    authority.join();
    return ok ? 0 : 3;
}
