#include <QApplication>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QProcess>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QVBoxLayout>

class Window final : public QMainWindow {
    QComboBox* drives{}; QLabel* imageLabel{}; QProgressBar* progress{}; QString imagePath;
    void scan() {
        drives->clear(); QDir sys("/sys/class/block");
        for (const QString& name : sys.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (name.startsWith("loop") || name.startsWith("ram") || name.startsWith("zram")) continue;
            QFile removable("/sys/class/block/" + name + "/removable");
            if (!removable.open(QIODevice::ReadOnly) || removable.readAll().trimmed() != "1") continue;
            QFile sizeFile("/sys/class/block/" + name + "/size"); if (!sizeFile.open(QIODevice::ReadOnly)) continue;
            const qulonglong sectors = sizeFile.readAll().trimmed().toULongLong();
            drives->addItem(QString("/dev/%1 — %2 GB").arg(name).arg((sectors * 512.0) / 1e9, 0, 'f', 1), "/dev/" + name);
        }
    }
public:
    Window() {
        setWindowTitle("ISO Flash 2.0"); resize(560, 260); auto* root = new QWidget; auto* layout = new QVBoxLayout(root);
        layout->addWidget(new QLabel("<h2>ISO Flash 2.0</h2><p>Запись Linux ISO на USB-накопитель</p>"));
        auto* pick = new QPushButton("Выбрать ISO-образ"); imageLabel = new QLabel("Образ не выбран"); layout->addWidget(pick); layout->addWidget(imageLabel);
        auto* row = new QHBoxLayout; drives = new QComboBox; auto* refresh = new QPushButton("Обновить"); row->addWidget(drives, 1); row->addWidget(refresh); layout->addLayout(row);
        progress = new QProgressBar; progress->setRange(0, 100); layout->addWidget(progress);
        auto* write = new QPushButton("ЗАПИСАТЬ"); write->setStyleSheet("font-weight:bold;padding:10px"); layout->addWidget(write); setCentralWidget(root);
        connect(pick, &QPushButton::clicked, this, [this] { const QString f = QFileDialog::getOpenFileName(this, "Выбрать ISO", QDir::homePath(), "ISO images (*.iso);;All files (*)"); if (!f.isEmpty()) { imagePath = f; imageLabel->setText(QFileInfo(f).fileName()); } });
        connect(refresh, &QPushButton::clicked, this, [this] { scan(); });
        connect(write, &QPushButton::clicked, this, [this] {
            if (imagePath.isEmpty() || drives->currentIndex() < 0) { QMessageBox::warning(this, "ISO Flash", "Выберите ISO и USB-накопитель."); return; }
            const QString device = drives->currentData().toString();
            if (QMessageBox::warning(this, "Уничтожение данных", "Все данные на " + device + " будут уничтожены. Продолжить?", QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
            QProcess process; process.start("pkexec", {"/usr/lib/iso-flash/iso-flash-write", "--write", imagePath, device});
            if (!process.waitForStarted()) { QMessageBox::critical(this, "Ошибка", "Не удалось запустить модуль записи."); return; }
            progress->setRange(0, 0); QByteArray pending;
            while (process.state() != QProcess::NotRunning) {
                process.waitForReadyRead(100); pending += process.readAllStandardError();
                int pos; while ((pos = pending.indexOf('\n')) >= 0) { const QByteArray line = pending.left(pos); pending.remove(0, pos + 1); const auto parts = line.split(' '); if (parts.size() == 3 && parts[0] == "PROGRESS") { const double done = parts[1].toDouble(), total = parts[2].toDouble(); progress->setRange(0, 100); progress->setValue(total > 0 ? static_cast<int>(done * 100.0 / total) : 0); } }
                qApp->processEvents();
            }
            progress->setRange(0, 100);
            if (process.exitCode() == 0) QMessageBox::information(this, "Готово", "Запись завершена; данные сброшены на устройство.");
            else QMessageBox::critical(this, "Ошибка записи", QString::fromLocal8Bit(process.readAllStandardError()));
            scan();
        });
        scan();
    }
};
int main(int argc, char** argv) { QApplication app(argc, argv); Window window; window.show(); return app.exec(); }
