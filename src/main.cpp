// SPDX-FileCopyrightText: 2003, 2006 KoolDock team
// SPDX-FileCopyrightText: 2025 Matias Fernandez <radix@kde.cl>
// SPDX-License-Identifier: GPL-2.0-or-later

#include <KAboutData>
#include <KCrash>
#include <KDBusService>
#include <KLocalizedString>

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDBusInterface>
#include <QQuickWindow>
#include <QSurfaceFormat>

#include "kooldock.h"
#include "kooldocksettings.h"
#include "version.h"

int main(int argc, char *argv[])
{
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setAlphaBufferSize(8);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("kooldock2"));
    app.setOrganizationDomain(QStringLiteral("kde.cl"));
    app.setApplicationVersion(QString::fromLatin1(KOOLDOCK_VERSION));
    app.setQuitOnLastWindowClosed(false);

    KLocalizedString::setApplicationDomain("kooldock2");

    KAboutData aboutData(
        QStringLiteral("kooldock2"),
        i18n("KoolDock"),
        QString::fromLatin1(KOOLDOCK_VERSION),
        i18n("A Kool Dock for KDE Plasma"),
        KAboutLicense::GPL_V2,
        i18n("(c) 2003, 2006 KoolDock team; (c) 2025 KoolDock2 port"),
        QString(),
        QStringLiteral("https://gitlab.com/radixcl/kooldock2"),
        QStringLiteral("kooldock2-devel@lists.kde.cl"));
    aboutData.addAuthor(i18n("Matias Fernandez"), i18n("Original author"), QStringLiteral("radix@kde.cl"));
    aboutData.addAuthor(i18n("Francisco Guidi"), i18n("Original author"), QStringLiteral("francisco@guidi.com"));
    aboutData.addAuthor(i18n("Blase Stanek"), i18n("Development after v0.3"), QStringLiteral("bisiek@op.pl"));
    aboutData.addCredit(i18n("Mauricio Bahamonde"), i18n("Project webmaster"), QStringLiteral("elkrammer@kde.cl"));
    aboutData.addCredit(i18n("Sebastian Sariego Benitez"), i18n("Icon and artwork"), QStringLiteral("segfault@powers.cl"));
    aboutData.setTranslator(i18nc("NAME OF TRANSLATORS", "Your names"),
                            i18nc("EMAIL OF TRANSLATORS", "Your emails"));
    KAboutData::setApplicationData(aboutData);

    QCommandLineParser parser;
    parser.setApplicationDescription(aboutData.shortDescription());
    QCommandLineOption optionsOption(QStringList() << QStringLiteral("o") << QStringLiteral("options"),
        i18n("Show configuration window on start"));
    QCommandLineOption killOption(QStringList() << QStringLiteral("k") << QStringLiteral("kill"),
        i18n("Kill all running kooldock2 instances"));
    QCommandLineOption debugBoundsOption(QStringList() << QStringLiteral("d") << QStringLiteral("debug"),
        i18n("Enable debug logging and draw the dock window's real (otherwise invisible) bounds"));
    parser.addOption(optionsOption);
    parser.addOption(killOption);
    parser.addOption(debugBoundsOption);
    aboutData.setupCommandLine(&parser);
    parser.process(app);

    if (parser.isSet(killOption)) {
        // KDBusService is unique: just quit the registered instance via
        // QDBus. The bus name matches QGuiApplication::desktopFileName()
        // (derived by KAboutData::setApplicationData() above from the
        // homepage URL, e.g. "com.github.kooldock2" — not the literal
        // "org.kde.koolock2" this used to hardcode, which never matched
        // any real service). KDBusService exposes quit() for free on
        // /MainApplication via the standard Qt QCoreApplication D-Bus
        // interface, so no custom service/method is needed either.
        QDBusInterface dbus(app.desktopFileName(),
                            QStringLiteral("/MainApplication"),
                            QStringLiteral("org.qtproject.Qt.QCoreApplication"));
        if (dbus.isValid()) {
            dbus.call(QStringLiteral("quit"));
        }
        return 0;
    }

    KCrash::initialize();

    KDBusService dbusService(KDBusService::Unique);
    QObject::connect(&dbusService, &KDBusService::activateRequested,
                     &app, [](const QStringList &args, const QString &workingDirectory) {
                         Q_UNUSED(workingDirectory);
                         if (args.contains(QStringLiteral("--options")) ||
                             args.contains(QStringLiteral("-o"))) {
                             KoolDock::instance()->showPreferences();
                         }
                     });

    KoolDock::create(&app, parser.isSet(optionsOption), parser.isSet(debugBoundsOption));

    return app.exec();
}
