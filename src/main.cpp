#include "gui/mainwindow.h"
#include "presets.h"
#include "serialtransport.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QTimer>
#ifdef Q_OS_WIN
#include <cstdio>
#include <windows.h>
#endif

// rt860cps                         start the editor
// rt860cps <file.88dat>            open a file
// rt860cps --dump <port> <file>    read the radio to a .88dat file, no window
int main(int argc, char **argv) {
#ifdef Q_OS_WIN
    // A Windows program with a window has no console. When started from a command
    // prompt, print there, unless the output already goes to a file or a pipe.
    if (::AttachConsole(ATTACH_PARENT_PROCESS)) {
        if (!::GetStdHandle(STD_OUTPUT_HANDLE) || _fileno(stdout) < 0) std::freopen("CONOUT$", "w", stdout);
        if (!::GetStdHandle(STD_ERROR_HANDLE) || _fileno(stderr) < 0) std::freopen("CONOUT$", "w", stderr);
    }
#endif
    QTextStream out(stdout), err(stderr);
    if (argc == 2 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--version")) {
        out << "rt860cps " << RT860_VERSION << Qt::endl;
        return 0;
    }
    if (argc == 4 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--dump")) {
        try {
            rt860::SerialTransport transport(QString::fromLocal8Bit(argv[2]));
            rt860::Radio radio(transport);
            radio.connect();
            const QByteArray image = radio.readImage([&](int d, int t) {
                if (d % 23 == 0 || d == t) out << "\rreading " << d * 100 / t << "%" << Qt::flush;
            });
            radio.end();
            out << Qt::endl;
            QFile f(QString::fromLocal8Bit(argv[3]));
            if (!f.open(QIODevice::WriteOnly) || f.write(image) != image.size()) {
                err << "cannot write " << argv[3] << Qt::endl;
                return 1;
            }
            int used = 0;
            for (int i = 0; i < rt860::ChannelCount; ++i) used += !rt860::decodeChannel(image, i).empty;
            out << "saved " << image.size() << " bytes, " << used << " channels in use" << Qt::endl;
            return 0;
        } catch (const std::exception &e) {
            err << Qt::endl << e.what() << Qt::endl;
            return 1;
        }
    }

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("rt860cps"));
    QCoreApplication::setApplicationName(QStringLiteral("rt860cps"));
    {
        // Shipped lists: where the package puts them, else the source tree this was built from.
        QString shipped = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral(RT860_LISTS_FROM_BIN));
        if (!QDir(shipped).exists()) shipped = QStringLiteral(RT860_LISTS_SOURCE_DIR);
        rt860::setPresetDirs({QDir::cleanPath(shipped), MainWindow::userListsDir()});
    }
    MainWindow window;
    window.show();
    const QStringList args = app.arguments();
    QString screenshot;
    for (int i = 1; i < args.size(); ++i) {
        if (args.at(i) == QLatin1String("--screenshot") && i + 1 < args.size()) screenshot = args.at(++i);   // for testing
        else if (args.at(i) == QLatin1String("--tab") && i + 1 < args.size()) window.showTab(args.at(++i).toInt());      // for testing
        else if (args.at(i) == QLatin1String("--import")) QTimer::singleShot(100, &window, [&window] { window.importPresets(qEnvironmentVariable("RT860_IMPORT_TICK").split(',', Qt::SkipEmptyParts)); });   // for testing
        else window.openFile(args.at(i));
    }
    if (!screenshot.isEmpty())
        QTimer::singleShot(400, &window, [&] {
            QWidget *modal = QApplication::activeModalWidget();
            (modal ? modal : &window)->grab().save(screenshot);
            if (modal) modal->close();
            app.quit();
        });
    return app.exec();
}
