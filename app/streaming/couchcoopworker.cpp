#include "couchcoopworker.h"

#include "backend/computermanager.h"
#include "backend/computerseeker.h"
#include "backend/nvcomputer.h"
#include "cli/commandlineparser.h"
#include "settings/streamingpreferences.h"
#include "streaming/session.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QReadWriteLock>
#include <QTextStream>

namespace {

void reportReady()
{
    QJsonObject object;
    object.insert(QStringLiteral("ready"), true);
    QTextStream(stdout) << QJsonDocument(object).toJson(QJsonDocument::Compact)
                         << Qt::endl;
}

void reportError(const QString& message)
{
    QJsonObject object;
    object.insert(QStringLiteral("error"), message);
    QTextStream(stdout) << QJsonDocument(object).toJson(QJsonDocument::Compact)
                         << Qt::endl;
}

}

int runCouchCoopWorker(const CoopWorkerCommandLineParser& parser,
                       StreamingPreferences* preferences)
{
    QCoreApplication* application = QCoreApplication::instance();
    if (application == nullptr || preferences == nullptr) {
        reportError(QStringLiteral("Worker application initialization failed"));
        return 1;
    }

    qputenv("MOONLIGHT_COOP_PANE", QByteArray::number(parser.getPane()));
    qputenv("MOONLIGHT_COOP_WIDTH", QByteArray::number(parser.getWidth()));
    qputenv("MOONLIGHT_COOP_HEIGHT", QByteArray::number(parser.getHeight()));
    qputenv("MOONLIGHT_COOP_CONTROLLER", parser.getControllerPath().toUtf8());
    qputenv("SDL_VIDEODRIVER", "x11");

    preferences->windowMode = StreamingPreferences::WM_WINDOWED;
    preferences->width = parser.getWidth();
    preferences->height = parser.getHeight();
    preferences->bitrateKbps = preferences->getDefaultBitrate(
        preferences->width, preferences->height, preferences->fps, preferences->enableYUV444);
    preferences->quitAppAfter = false;
    preferences->backgroundGamepad = true;
    preferences->muteOnFocusLoss = false;
    preferences->multiController = false;

    auto* manager = new ComputerManager(preferences);
    manager->setParent(application);
    auto* seeker = new ComputerSeeker(manager, parser.getHost(), application);

    QObject::connect(seeker, &ComputerSeeker::errorTimeout, application, [application]() {
        reportError(QStringLiteral("Timed out while finding the selected PC"));
        application->quit();
    });

    QObject::connect(seeker, &ComputerSeeker::computerFound, application,
                     [application, preferences, parser](NvComputer* computer) {
        NvApp selectedApp;
        {
            QReadLocker locker(&computer->lock);
            if (computer->pairState != NvComputer::PS_PAIRED ||
                computer->state != NvComputer::CS_ONLINE) {
                reportError(QStringLiteral("The selected PC is offline or not paired"));
                application->quit();
                return;
            }
            if (computer->currentGameId != 0 && computer->currentGameId != parser.getAppId()) {
                reportError(QStringLiteral("The selected PC is already streaming another app"));
                application->quit();
                return;
            }
            for (const NvApp& app : computer->appList) {
                if (app.id == parser.getAppId() && app.isInitialized() && !app.hidden) {
                    selectedApp = app;
                    break;
                }
            }
        }

        if (!selectedApp.isInitialized()) {
            reportError(QStringLiteral("The selected app is no longer available on the PC"));
            application->quit();
            return;
        }

        auto* session = new Session(computer, selectedApp, preferences);
        session->setParent(application);
        QObject::connect(session, &Session::streamWindowReady, application, []() {
            reportReady();
        });
        QObject::connect(session, &Session::displayLaunchError, application,
                         [](const QString& message) { reportError(message); });
        QObject::connect(session, &Session::stageFailed, application,
                         [](const QString& stage, int errorCode, const QString&) {
            reportError(QStringLiteral("%1 failed with error %2").arg(stage).arg(errorCode));
        });
        QObject::connect(session, &Session::sessionFinished, application,
                         [application](int) { application->quit(); });
        QObject::connect(session, &Session::readyForDeletion, session, &QObject::deleteLater);

        if (!session->initialize(nullptr)) {
            reportError(QStringLiteral("Unable to initialize the co-op stream"));
            session->deleteLater();
            application->quit();
            return;
        }
        session->start();
    });

    seeker->start(30000);
    return application->exec();
}

