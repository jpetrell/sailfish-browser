/****************************************************************************
**
** Copyright (C) 2013 Jolla Ltd.
** Contact: Vesa-Matti Hartikainen <vesa-matti.hartikainen@jollamobile.com>
**
****************************************************************************/

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <QGuiApplication>
#include <QQuickView>
#include <QQmlContext>
#include <QQmlEngine>
#include <QtQml>
#include <QTimer>
#include <QTranslator>
#include <QDir>
#include <QScreen>
#include <QDBusConnection>

//#include "qdeclarativemozview.h"
#include "quickmozview.h"
#include "qmozcontext.h"

#include "declarativebookmarkmodel.h"
#include "declarativewebutils.h"
#include "browserservice.h"
#include "downloadmanager.h"
#include "settingmanager.h"
#include "closeeventfilter.h"
#include "declarativetab.h"
#include "declarativetabmodel.h"
#include "declarativehistorymodel.h"
#include "declarativewebcontainer.h"

#ifdef HAS_BOOSTER
#include <MDeclarativeCache>
#endif

Q_DECL_EXPORT int main(int argc, char *argv[])
{
    // EGL FPS are lower with threaded render loop
    // that's why this workaround.
    // See JB#7358
    setenv("QML_BAD_GUI_RENDER_LOOP", "1", 1);
    setenv("USE_ASYNC", "1", 1);

    // Workaround for https://bugzilla.mozilla.org/show_bug.cgi?id=929879
    setenv("LC_NUMERIC", "C", 1);
    setlocale(LC_NUMERIC, "C");
#ifdef HAS_BOOSTER
    QScopedPointer<QGuiApplication> app(MDeclarativeCache::qApplication(argc, argv));
    QScopedPointer<QQuickView> view(MDeclarativeCache::qQuickView());
#else
    QScopedPointer<QGuiApplication> app(new QGuiApplication(argc, argv));
    QScopedPointer<QQuickView> view(new QQuickView);
#endif
    app->setQuitOnLastWindowClosed(false);

    // TODO : Remove this and set custom user agent always
    // Don't set custom user agent string when arguments contains -developerMode, give url as last argument
    if (!app->arguments().contains("-developerMode")) {
        setenv("CUSTOM_UA", "Mozilla/5.0 (Maemo; Linux; U; Jolla; Sailfish; Mobile; rv:29.0) Gecko/29.0 Firefox/29.0 SailfishBrowser/1.0", 1);
    }

    BrowserService *service = new BrowserService(app.data());
    // Handle command line launch
    if (!service->registered()) {
        QDBusMessage message = QDBusMessage::createMethodCall(service->serviceName(), "/",
                                                              service->serviceName(), "openUrl");
        QStringList args;
        // Pass url argument if given
        if (app->arguments().count() > 1) {
            args << app->arguments().at(1);
        }
        message.setArguments(QVariantList() << args);

        QDBusConnection::sessionBus().asyncCall(message);
        if (QCoreApplication::hasPendingEvents()) {
            QCoreApplication::processEvents();
        }

        return 0;
    }

    QString translationPath("/usr/share/translations/");
    QTranslator engineeringEnglish;
    engineeringEnglish.load("sailfish-browser_eng_en", translationPath);
    qApp->installTranslator(&engineeringEnglish);

    QTranslator translator;
    translator.load(QLocale(), "sailfish-browser", "-", translationPath);
    qApp->installTranslator(&translator);

    //% "Browser"
    view->setTitle(qtTrId("sailfish-browser-ap-name"));

    qmlRegisterType<DeclarativeBookmarkModel>("Sailfish.Browser", 1, 0, "BookmarkModel");
    qmlRegisterType<DeclarativeTabModel>("Sailfish.Browser", 1, 0, "TabModel");
    qmlRegisterType<DeclarativeHistoryModel>("Sailfish.Browser", 1, 0, "HistoryModel");
    qmlRegisterType<DeclarativeTab>("Sailfish.Browser", 1, 0, "Tab");
    qmlRegisterType<DeclarativeWebContainer>("Sailfish.Browser", 1, 0, "WebContainer");

    QString componentPath(DEFAULT_COMPONENTS_PATH);
    QMozContext::GetInstance()->addComponentManifest(componentPath + QString("/components/EmbedLiteBinComponents.manifest"));
    QMozContext::GetInstance()->addComponentManifest(componentPath + QString("/components/EmbedLiteJSComponents.manifest"));
    QMozContext::GetInstance()->addComponentManifest(componentPath + QString("/chrome/EmbedLiteJSScripts.manifest"));
    QMozContext::GetInstance()->addComponentManifest(componentPath + QString("/chrome/EmbedLiteOverrides.manifest"));

    app->setApplicationName(QString("sailfish-browser"));
    app->setOrganizationName(QString("org.sailfishos"));

    DeclarativeWebUtils * utils = new DeclarativeWebUtils(app->arguments(), service, app.data());
    utils->clearStartupCacheIfNeeded();
    view->rootContext()->setContextProperty("WebUtils", utils);
    view->rootContext()->setContextProperty("MozContext", QMozContext::GetInstance());

    DownloadManager  * dlMgr = new DownloadManager(service, app.data());
    CloseEventFilter * clsEventFilter = new CloseEventFilter(dlMgr, app.data());
    view->installEventFilter(clsEventFilter);
    QObject::connect(service, SIGNAL(openUrlRequested(QString)),
                     clsEventFilter, SLOT(cancelStopApplication()));

    SettingManager * settingMgr = new SettingManager(app.data());
    QObject::connect(QMozContext::GetInstance(), SIGNAL(onInitialized()),
                     settingMgr, SLOT(initialize()));

    QObject::connect(QMozContext::GetInstance(), SIGNAL(newWindowRequested(QString)),
                     utils, SLOT(openUrl(QString)));

#ifdef USE_RESOURCES
    view->setSource(QUrl("qrc:///browser.qml"));
#else
    bool isDesktop = qApp->arguments().contains("-desktop");

    QString path;
    if (isDesktop) {
        path = qApp->applicationDirPath() + QDir::separator();
    } else {
        path = QString(DEPLOYMENT_PATH);
    }
    view->setSource(QUrl::fromLocalFile(path+"browser.qml"));
#endif

    view->showFullScreen();

    // Setup embedding
    QTimer::singleShot(0, QMozContext::GetInstance(), SLOT(runEmbedding()));

    return app->exec();
}
