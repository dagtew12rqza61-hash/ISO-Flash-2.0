#include <QApplication>
#include <QComboBox>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QProgressBar>
#include <QPushButton>
#include <QDir>
#include <QFile>
#include <QVariantMap>
#include <QVBoxLayout>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

class Window final : public QMainWindow {
    Q_OBJECT

    QComboBox* drives{};
    QLabel* imageLabel{};
    QLabel* statusLabel{};
    QProgressBar* progress{};
    QPushButton* writeButton{};
    QPushButton* cancelButton{};
    QString imagePath;
    qulonglong activeJob = 0;

    struct ProgressEvent {
        QString state;
        qulonglong completed = 0;
        qulonglong total = 0;
    };
    struct FinishedEvent {
        QString state;
        qulonglong completed = 0;
        qulonglong total = 0;
        int errorNumber = 0;
        QString message;
    };
    QHash<qulonglong, ProgressEvent> pendingProgress;
    QHash<qulonglong, FinishedEvent> pendingFinished;

    static constexpr const char* serviceName = "org.iso_flash.Manager1";
    static constexpr const char* objectPath = "/org/iso_flash/Manager1";
    static constexpr const char* interfaceName = "org.iso_flash.Manager1";

    QDBusInterface manager() const {
        return QDBusInterface(serviceName, objectPath, interfaceName,
                              QDBusConnection::systemBus());
    }

    void update_write_enabled() {
        writeButton->setEnabled(!imagePath.isEmpty() &&
                                drives->currentIndex() >= 0 &&
                                activeJob == 0);
        cancelButton->setEnabled(activeJob != 0);
    }

    void scan() {
        drives->clear();
        QDBusInterface api = manager();
        if (!api.isValid()) {
            statusLabel->setText("Системная служба ISO Flash недоступна");
            update_write_enabled();
            return;
        }

        QDir sys("/sys/class/block");
        for (const QString& name :
             sys.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (name.startsWith("loop") || name.startsWith("ram") ||
                name.startsWith("zram")) {
                continue;
            }

            QFile removable("/sys/class/block/" + name + "/removable");
            if (!removable.open(QIODevice::ReadOnly) ||
                removable.readAll().trimmed() != "1") {
                continue;
            }

            const QByteArray devicePath = ("/dev/" + name).toLocal8Bit();
            struct stat metadata {};
            if (::stat(devicePath.constData(), &metadata) != 0 ||
                !S_ISBLK(metadata.st_mode)) {
                continue;
            }

            const uint deviceMajor = static_cast<uint>(major(metadata.st_rdev));
            const uint deviceMinor = static_cast<uint>(minor(metadata.st_rdev));
            const QDBusMessage reply = api.call(
                "InspectTarget", QVariant::fromValue(deviceMajor),
                QVariant::fromValue(deviceMinor));
            if (reply.type() == QDBusMessage::ErrorMessage ||
                reply.arguments().size() != 3) {
                continue;
            }

            const QList<QVariant> values = reply.arguments();
            const bool accepted = values.at(0).toBool();
            const qulonglong capacity = values.at(1).toULongLong();
            const QString reason = values.at(2).toString();
            if (!accepted || capacity == 0) {
                continue;
            }

            const double gib = static_cast<double>(capacity) /
                               (1024.0 * 1024.0 * 1024.0);
            const QVariantMap identity{
                {"major", deviceMajor}, {"minor", deviceMinor}};
            drives->addItem(
                QString("%1 — %2 GiB (проверен службой)")
                    .arg(name).arg(gib, 0, 'f', 1),
                identity);
            (void)reason;
        }

        if (drives->count() == 0) {
            statusLabel->setText(
                "Нет доступных USB-накопителей. Устройство должно пройти "
                "проверку безопасности службы.");
        } else {
            statusLabel->setText("Устройства проверены службой безопасности");
        }
        update_write_enabled();
    }

    void select_image() {
        const QString selected = QFileDialog::getOpenFileName(
            this, "Выбрать ISO-образ", QDir::homePath(),
            "ISO images (*.iso);;All files (*)");
        if (selected.isEmpty()) {
            return;
        }

        imagePath = selected;
        imageLabel->setText(QFileInfo(selected).fileName());
        statusLabel->setText("ISO выбран. Проверка будет выполнена службой перед записью.");
        update_write_enabled();
    }

    void start_write() {
        if (imagePath.isEmpty() || drives->currentIndex() < 0 ||
            activeJob != 0) {
            return;
        }

        const QVariantMap target = drives->currentData().toMap();
        const uint deviceMajor = target.value("major").toUInt();
        const uint deviceMinor = target.value("minor").toUInt();
        if (deviceMajor == 0) {
            statusLabel->setText("Некорректная идентификация USB-устройства");
            return;
        }

        const QByteArray path = imagePath.toLocal8Bit();
        const int rawFd = ::open(path.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (rawFd < 0) {
            statusLabel->setText(
                QString("Не удалось открыть ISO: %1").arg(QString::fromLocal8Bit(strerror(errno))));
            return;
        }

        struct stat metadata {};
        if (::fstat(rawFd, &metadata) != 0 || !S_ISREG(metadata.st_mode) ||
            metadata.st_size <= 0) {
            ::close(rawFd);
            statusLabel->setText("ISO должен быть обычным непустым файлом");
            return;
        }

        QDBusUnixFileDescriptor imageDescriptor(rawFd);
        ::close(rawFd);
        if (!imageDescriptor.isValid()) {
            statusLabel->setText("Не удалось передать ISO через Unix FD");
            return;
        }

        QDBusMessage message = QDBusMessage::createMethodCall(
            serviceName, objectPath, interfaceName, "StartWrite");
        message << QVariant::fromValue(imageDescriptor)
                << QVariant::fromValue(deviceMajor)
                << QVariant::fromValue(deviceMinor);

        statusLabel->setText("Проверка ISO и запрос авторизации Polkit…");
        writeButton->setEnabled(false);
        auto* watcher = new QDBusPendingCallWatcher(
            QDBusConnection::systemBus().asyncCall(message, 120000), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this](QDBusPendingCallWatcher* finished) {
            QDBusPendingReply<bool, qulonglong, QString> reply = *finished;
            if (reply.isError()) {
                statusLabel->setText(
                    "StartWrite: " + reply.error().message());
            } else if (!reply.argumentAt<0>() ||
                       reply.argumentAt<1>() == 0) {
                statusLabel->setText(
                    "Запись отклонена: " + reply.argumentAt<2>());
            } else {
                activeJob = reply.argumentAt<1>();
                progress->setValue(0);
                statusLabel->setText(
                    QString("Задача %1: запись запущена").arg(activeJob));
                if (pendingProgress.contains(activeJob)) {
                    const ProgressEvent event = pendingProgress.take(activeJob);
                    onProgress(activeJob, event.state, event.completed, event.total);
                }
                if (pendingFinished.contains(activeJob)) {
                    const FinishedEvent event = pendingFinished.take(activeJob);
                    onFinished(activeJob, event.state, event.completed,
                               event.total, event.errorNumber, event.message);
                }
            }
            finished->deleteLater();
            update_write_enabled();
        });
    }

    void cancel_write() {
        if (activeJob == 0) {
            return;
        }

        QDBusMessage message = QDBusMessage::createMethodCall(
            serviceName, objectPath, interfaceName, "CancelWrite");
        message << QVariant::fromValue(activeJob);
        statusLabel->setText("Отправка запроса безопасной отмены…");
        auto* watcher = new QDBusPendingCallWatcher(
            QDBusConnection::systemBus().asyncCall(message, 10000), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this](QDBusPendingCallWatcher* finished) {
            QDBusPendingReply<bool, QString> reply = *finished;
            if (reply.isError()) {
                statusLabel->setText(
                    "Ошибка отмены: " + reply.error().message());
            } else if (!reply.argumentAt<0>()) {
                statusLabel->setText(
                    "Отмена не принята: " + reply.argumentAt<1>());
            } else {
                statusLabel->setText(
                    "Отмена принята. Ожидание безопасного завершения операции…");
            }
            finished->deleteLater();
        });
    }

private slots:
    void onProgress(qulonglong jobId, const QString& state,
                    qulonglong completed, qulonglong total) {
        if (jobId != activeJob) {
            if (activeJob == 0) {
                pendingProgress.insert(jobId, {state, completed, total});
            }
            return;
        }
        const int percent = total == 0 ? 0 : static_cast<int>(
            (static_cast<long double>(completed) * 100.0L) /
            static_cast<long double>(total));
        progress->setValue(qBound(0, percent, 100));
        statusLabel->setText(
            QString("%1 — %2 / %3 MiB")
                .arg(state)
                .arg(completed / (1024.0 * 1024.0), 0, 'f', 1)
                .arg(total / (1024.0 * 1024.0), 0, 'f', 1));
    }

    void onFinished(qulonglong jobId, const QString& state,
                    qulonglong completed, qulonglong total,
                    int errorNumber, const QString& message) {
        if (jobId != activeJob) {
            if (activeJob == 0) {
                pendingFinished.insert(
                    jobId, {state, completed, total, errorNumber, message});
            }
            return;
        }
        activeJob = 0;
        pendingProgress.remove(jobId);
        pendingFinished.remove(jobId);
        progress->setValue(state == "completed" ? 100 : progress->value());
        statusLabel->setText(
            QString("%1 — %2 / %3 MiB. %4")
                .arg(state)
                .arg(completed / (1024.0 * 1024.0), 0, 'f', 1)
                .arg(total / (1024.0 * 1024.0), 0, 'f', 1)
                .arg(message));
        if (errorNumber != 0) {
            statusLabel->setToolTip(
                QString("errno=%1: %2").arg(errorNumber).arg(message));
        }
        update_write_enabled();
    }

public:
    Window() {
        setWindowTitle("ISO Flash 2.0");
        resize(680, 360);

        auto* root = new QWidget;
        auto* layout = new QVBoxLayout(root);
        layout->addWidget(new QLabel(
            "<h2>ISO Flash 2.0</h2>"
            "<p>Безопасная запись Linux ISO на USB-накопители</p>"));

        auto* pick = new QPushButton("Выбрать ISO-образ");
        imageLabel = new QLabel("Образ не выбран");
        layout->addWidget(pick);
        layout->addWidget(imageLabel);

        auto* row = new QHBoxLayout;
        drives = new QComboBox;
        auto* refresh = new QPushButton("Обновить устройства");
        row->addWidget(drives, 1);
        row->addWidget(refresh);
        layout->addLayout(row);

        progress = new QProgressBar;
        progress->setRange(0, 100);
        progress->setValue(0);
        layout->addWidget(progress);

        statusLabel = new QLabel("Ожидание");
        statusLabel->setWordWrap(true);
        layout->addWidget(statusLabel);

        auto* actions = new QHBoxLayout;
        writeButton = new QPushButton("ЗАПИСАТЬ");
        cancelButton = new QPushButton("Отменить запись");
        cancelButton->setEnabled(false);
        actions->addWidget(writeButton);
        actions->addWidget(cancelButton);
        layout->addLayout(actions);
        setCentralWidget(root);

        connect(pick, &QPushButton::clicked, this, [this] {
            select_image();
        });
        connect(refresh, &QPushButton::clicked, this, [this] {
            scan();
        });
        connect(writeButton, &QPushButton::clicked, this, [this] {
            start_write();
        });
        connect(cancelButton, &QPushButton::clicked, this, [this] {
            cancel_write();
        });
        connect(drives, &QComboBox::currentIndexChanged, this, [this](int) {
            update_write_enabled();
        });

        const bool progressConnected = QDBusConnection::systemBus().connect(
            serviceName, objectPath, interfaceName, "Progress", this,
            SLOT(onProgress(qulonglong,QString,qulonglong,qulonglong)));
        const bool finishedConnected = QDBusConnection::systemBus().connect(
            serviceName, objectPath, interfaceName, "Finished", this,
            SLOT(onFinished(qulonglong,QString,qulonglong,qulonglong,int,QString)));
        if (!progressConnected || !finishedConnected) {
            statusLabel->setText("Не удалось подписаться на события D-Bus службы");
        }
        scan();
    }
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    Window window;
    window.show();
    return app.exec();
}

#include "main.moc"
