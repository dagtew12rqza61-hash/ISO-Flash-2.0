#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusUnixFileDescriptor>
#include <QVariant>

#include <fcntl.h>
#include <iostream>
#include <unistd.h>

namespace {
constexpr const char* service_name = "org.iso_flash.Manager1";
constexpr const char* object_path = "/org/iso_flash/Manager1";
constexpr const char* interface_name = "org.iso_flash.Manager1";
constexpr const char* expected_sha256 =
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

int fail(const char* message, const QString& detail = {}) {
    std::cerr << message;
    if (!detail.isEmpty()) std::cerr << ": " << detail.toStdString();
    std::cerr << '\n';
    return 1;
}
}

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    if (!QDBusConnection::systemBus().isConnected())
        return fail("QtDBus system-bus connection is unavailable");

    char path[] = "/tmp/iso-flash-qt-dbus-XXXXXX";
    const int created = ::mkstemp(path);
    if (created < 0) return fail("Cannot create QtDBus descriptor test file");
    if (::write(created, "abc", 3) != 3) {
        ::close(created);
        ::unlink(path);
        return fail("Cannot write QtDBus descriptor test data");
    }
    ::close(created);

    const int raw_fd = ::open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (raw_fd < 0) {
        ::unlink(path);
        return fail("Cannot reopen QtDBus test file read-only");
    }
    QDBusUnixFileDescriptor image_fd(raw_fd);
    ::close(raw_fd);
    if (!image_fd.isValid()) {
        ::unlink(path);
        return fail("QtDBus did not retain the Unix file descriptor");
    }

    QDBusInterface manager(service_name, object_path, interface_name,
                            QDBusConnection::systemBus());
    if (!manager.isValid()) {
        ::unlink(path);
        return fail("ISO Flash D-Bus manager interface is unavailable",
                    manager.lastError().message());
    }

    const QDBusMessage validation =
        manager.call("ValidateImage", QVariant::fromValue(image_fd));
    if (validation.type() == QDBusMessage::ErrorMessage ||
        validation.arguments().size() != 4) {
        ::unlink(path);
        return fail("QtDBus Unix-FD ValidateImage call failed",
                    validation.errorMessage());
    }
    const QList<QVariant> validation_args = validation.arguments();
    if (!validation_args.at(0).toBool() ||
        validation_args.at(1).toULongLong() != 3 ||
        validation_args.at(2).toString() != expected_sha256) {
        ::unlink(path);
        return fail("QtDBus Unix-FD image validation returned an unexpected result",
                    validation_args.at(3).toString());
    }

    const QDBusMessage write_reply = manager.call(
        "StartWrite", QVariant::fromValue(image_fd),
        QVariant::fromValue(static_cast<uint>(1)),
        QVariant::fromValue(static_cast<uint>(3)));
    ::unlink(path);
    if (write_reply.type() == QDBusMessage::ErrorMessage ||
        write_reply.arguments().size() != 3) {
        return fail("QtDBus StartWrite call failed", write_reply.errorMessage());
    }

    const QList<QVariant> write_args = write_reply.arguments();
    const bool accepted = write_args.at(0).toBool();
    const qulonglong job_id = write_args.at(1).toULongLong();
    const QString reason = write_args.at(2).toString();
    if (accepted || job_id != 0 ||
        (!reason.contains("Polkit", Qt::CaseInsensitive) &&
         !reason.contains("auth", Qt::CaseInsensitive))) {
        return fail("Unprivileged QtDBus StartWrite was not rejected by Polkit",
                    reason);
    }

    std::cout << "QtDBus Unix-FD validation and Polkit-gated StartWrite denial passed\n";
    return 0;
}
