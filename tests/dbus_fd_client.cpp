#include <gio/gio.h>
#include <gio/gunixfdlist.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

bool call_validate(int fd, bool expected_valid) {
    GError* error = nullptr;
    GDBusConnection* connection =
        g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
    if (connection == nullptr) {
        std::cerr << (error != nullptr ? error->message : "D-Bus connection failed") << '\n';
        if (error != nullptr) g_error_free(error);
        return false;
    }

    GUnixFDList* fd_list = g_unix_fd_list_new();
    const int handle = g_unix_fd_list_append(fd_list, fd, &error);
    if (handle < 0) {
        std::cerr << (error != nullptr ? error->message : "FD append failed") << '\n';
        if (error != nullptr) g_error_free(error);
        g_object_unref(fd_list);
        g_object_unref(connection);
        return false;
    }

    GVariant* reply = g_dbus_connection_call_with_unix_fd_list_sync(
        connection, "org.iso_flash.Manager1", "/org/iso_flash/Manager1",
        "org.iso_flash.Manager1", "ValidateImage",
        g_variant_new("(h)", handle), G_VARIANT_TYPE("(btss)"),
        G_DBUS_CALL_FLAGS_NONE, 10000, fd_list, nullptr, nullptr, &error);

    bool ok = false;
    if (reply == nullptr) {
        std::cerr << (error != nullptr ? error->message : "ValidateImage call failed") << '\n';
        if (error != nullptr) g_error_free(error);
    } else {
        gboolean accepted = FALSE;
        guint64 size_bytes = 0;
        const gchar* digest = nullptr;
        const gchar* reason = nullptr;
        g_variant_get(reply, "(bt&s&s)", &accepted, &size_bytes, &digest, &reason);
        ok = accepted == expected_valid;
        if (expected_valid) {
            ok = ok && size_bytes == 3 &&
                 std::strcmp(digest, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0;
        }
        if (!ok) std::cerr << "Unexpected ValidateImage reply: " << reason << '\n';
        g_variant_unref(reply);
    }

    g_object_unref(fd_list);
    g_object_unref(connection);
    return ok;
}

}  // namespace

int main() {
    char path[] = "/tmp/iso-flash-dbus-fd-XXXXXX";
    const int created = ::mkstemp(path);
    if (created < 0) return 1;
    if (::write(created, "abc", 3) != 3) {
        ::close(created);
        ::unlink(path);
        return 1;
    }
    ::close(created);

    const int read_only = ::open(path, O_RDONLY | O_CLOEXEC);
    const int read_write = ::open(path, O_RDWR | O_CLOEXEC);
    ::unlink(path);
    if (read_only < 0 || read_write < 0) {
        if (read_only >= 0) ::close(read_only);
        if (read_write >= 0) ::close(read_write);
        return 1;
    }

    const bool read_only_ok = call_validate(read_only, true);
    const bool read_write_ok = call_validate(read_write, false);
    ::close(read_only);
    ::close(read_write);
    return read_only_ok && read_write_ok ? 0 : 1;
}
