#include "storage/settings_manager.h"
#include "storage/sqlite_manager.h"
#include "ui/mainwindow.h"

#include <QApplication>
#include <QMessageBox>

int main(int argc, char *argv[]) {
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("NanoPulse"));
    QApplication::setOrganizationName(QStringLiteral("NanoPulse"));
    QApplication::setApplicationVersion(QString::fromLatin1(NANOPULSE_VERSION));

    SqliteManager storage;
    if (!storage.open()) {
        QMessageBox::critical(nullptr, QObject::tr("NanoPulse"),
                              QObject::tr("Database initialization failed:\n%1")
                                  .arg(storage.lastError()));
        return 1;
    }
    SettingsManager settings;
    MainWindow window(&storage, &settings);
    window.show();
    return application.exec();
}
