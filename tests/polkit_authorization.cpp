#include "polkit_authorization.hpp"

#include <iostream>
#include <string>

int main() {
    std::uint64_t ticks = 0;
    if (!iso_flash::read_process_start_ticks(static_cast<guint32>(::getpid()), ticks) ||
        ticks == 0) {
        std::cerr << "Cannot read current process start time\n";
        return 1;
    }
    guint32 uid = 0;
    if (!iso_flash::read_process_real_uid(static_cast<guint32>(::getpid()), uid) ||
        uid != static_cast<guint32>(::getuid())) {
        std::cerr << "Cannot validate current process UID\n";
        return 2;
    }
    if (iso_flash::read_process_start_ticks(0, ticks) ||
        iso_flash::read_process_real_uid(0, uid)) {
        std::cerr << "PID zero must be rejected\n";
        return 3;
    }
    iso_flash::DbusCallerIdentity identity;
    GError* error = nullptr;
    if (iso_flash::query_dbus_caller_identity(nullptr, ":1.1", identity,
                                               nullptr, &error)) {
        std::cerr << "Null D-Bus connection must be rejected\n";
        return 4;
    }
    if (error != nullptr) g_error_free(error);
    return 0;
}
