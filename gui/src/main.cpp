#include <QApplication>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QVBoxLayout>

class Window final : public QMainWindow {
    QComboBox* drives{};
    QLabel* imageLabel{};
    QString imagePath;

    void scan() {
        drives->clear();
        QDir sys("/sys/class/block");
        for (const QString& name : sys.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (name.startsWith("loop") || name.startsWith("ram") || name.startsWith("zram")) continue;
            QFile removable("/sys/class/block/" + name + "/removable");
            if (!removable.open(QIODevice::ReadOnly) || removable.readAll().trimmed() != "1") continue;
            QFile sizeFile("/sys/class/block/" + name + "/size");
            if (!sizeFile.open(QIODevice::ReadOnly)) continue;
            const qulonglong sectors = sizeFile.readAll().trimmed().toULongLong();
            drives->addItem(QString("/dev/%1 — %2 GB (unverified candidate)")
                                .arg(name).arg((sectors * 512.0) / 1e9, 0, 'f', 1),
                            "/dev/" + name);
        }
    }

public:
    Window() {
        setWindowTitle("ISO Flash 2.0");
        resize(620, 300);
        auto* root = new QWidget;
        auto* layout = new QVBoxLayout(root);
        layout->addWidget(new QLabel(
            "<h2>ISO Flash 2.0</h2>"
            "<p>Linux ISO imaging — development build</p>"
            "<p style='color:#b45309'><b>Запись на физические устройства временно отключена.</b>"
            "<br>Сейчас доступны только просмотр накопителей и выбор образа.</p>"));
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
        auto* write = new QPushButton("ЗАПИСАТЬ — ПОКА НЕДОСТУПНО");
        write->setEnabled(false);
        layout->addWidget(write);
        setCentralWidget(root);

        connect(pick, &QPushButton::clicked, this, [this] {
            const QString f = QFileDialog::getOpenFileName(
                this, "Выбрать ISO", QDir::homePath(), "ISO images (*.iso);;All files (*)");
            if (!f.isEmpty()) {
                imagePath = f;
                imageLabel->setText(QFileInfo(f).fileName());
            }
        });
        connect(refresh, &QPushButton::clicked, this, [this] { scan(); });
        scan();
    }
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    Window window;
    window.show();
    return app.exec();
}
