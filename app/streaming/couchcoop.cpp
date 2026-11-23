#include "couchcoop.h"

#include "backend/computermanager.h"
#include "backend/nvcomputer.h"
#include "backend/nvapp.h"
#include "input/coopcontrollers.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QReadWriteLock>
#include <QTimer>

#include <utility>

#ifdef HAS_X11
#include <X11/Xlib.h>
#endif

CouchCoop::CouchCoop(QObject* parent)
    : QObject(parent)
{
    m_ControllerRefreshTimer = new QTimer(this);
    m_ControllerRefreshTimer->setInterval(500);
    connect(m_ControllerRefreshTimer, &QTimer::timeout,
            this, &CouchCoop::refreshControllers);
}

CouchCoop::~CouchCoop()
{
    stop();
}

bool CouchCoop::supported() const
{
#if defined(Q_OS_LINUX) && defined(HAS_X11)
    return QGuiApplication::platformName() == QStringLiteral("xcb") &&
           !qEnvironmentVariableIsEmpty("DISPLAY");
#else
    return false;
#endif
}

bool CouchCoop::active() const
{
    return m_Active;
}

QVariantList CouchCoop::hosts() const
{
    return m_Hosts;
}

QVariantList CouchCoop::controllers() const
{
    return m_Controllers;
}

void CouchCoop::initialize(ComputerManager* manager)
{
    if (m_Manager == manager) {
        refreshHosts();
        return;
    }

    if (m_Manager != nullptr) {
        disconnect(m_Manager, nullptr, this, nullptr);
    }

    m_Manager = manager;
    if (m_Manager != nullptr && !m_ControllerRefreshTimer->isActive()) {
        m_ControllerRefreshTimer->start();
    }
    if (m_Manager != nullptr) {
        connect(m_Manager, &ComputerManager::computerStateChanged,
                this, &CouchCoop::refreshHosts);
    }

    refreshHosts();
    refreshControllers();
}

void CouchCoop::refreshControllers()
{
    const QVariantList newControllers = CoopControllers::enumerate();
    if (newControllers != m_Controllers) {
        m_Controllers = newControllers;
        emit controllersChanged();
    }
}

void CouchCoop::refreshHosts()
{
    QVariantList newHosts;
    if (m_Manager != nullptr) {
        for (NvComputer* computer : m_Manager->getComputers()) {
            if (computer == nullptr) {
                continue;
            }

            QReadLocker locker(&computer->lock);
            if (computer->state != NvComputer::CS_ONLINE ||
                computer->pairState != NvComputer::PS_PAIRED ||
                !computer->isSupportedServerVersion) {
                continue;
            }

            QVariantList apps;
            for (const NvApp& app : computer->appList) {
                if (computer->currentGameId != 0 && computer->currentGameId != app.id) {
                    continue;
                }
                if (!app.isInitialized() || app.hidden) {
                    continue;
                }
                QVariantMap appMap;
                appMap.insert(QStringLiteral("id"), app.id);
                appMap.insert(QStringLiteral("name"), app.name);
                apps.append(appMap);
            }
            if (apps.isEmpty()) {
                continue;
            }

            QVariantMap hostMap;
            hostMap.insert(QStringLiteral("uuid"), computer->uuid);
            hostMap.insert(QStringLiteral("name"), computer->name);
            hostMap.insert(QStringLiteral("apps"), apps);
            hostMap.insert(QStringLiteral("runningApp"), computer->currentGameId);
            newHosts.append(hostMap);
        }
    }

    if (newHosts != m_Hosts) {
        m_Hosts = newHosts;
        emit hostsChanged();
    }
}

bool CouchCoop::hostAndApp(const QString& uuid, int appId,
                           NvComputer*& computer, NvApp& app) const
{
    computer = nullptr;
    if (m_Manager == nullptr) {
        return false;
    }

    for (NvComputer* candidate : m_Manager->getComputers()) {
        if (candidate == nullptr) {
            continue;
        }
        QReadLocker locker(&candidate->lock);
        if (candidate->uuid != uuid || candidate->state != NvComputer::CS_ONLINE ||
            candidate->pairState != NvComputer::PS_PAIRED || !candidate->isSupportedServerVersion) {
            continue;
        }
        for (const NvApp& candidateApp : candidate->appList) {
            if (candidateApp.id == appId && candidateApp.isInitialized() && !candidateApp.hidden) {
                computer = candidate;
                app = candidateApp;
                return true;
            }
        }
    }
    return false;
}

bool CouchCoop::start(QQuickWindow* window,
                      const QString& leftHost, int leftApp, const QString& leftController,
                      const QString& rightHost, int rightApp, const QString& rightController)
{
#if !defined(Q_OS_LINUX) || !defined(HAS_X11)
    Q_UNUSED(window);
    Q_UNUSED(leftHost); Q_UNUSED(leftApp); Q_UNUSED(leftController);
    Q_UNUSED(rightHost); Q_UNUSED(rightApp); Q_UNUSED(rightController);
    emit failed(tr("Couch co-op requires an X11 display."));
    return false;
#else
    if (!supported()) {
        emit failed(tr("Couch co-op requires Moonlight to run on an X11 display."));
        return false;
    }
    if (m_Active || window == nullptr) {
        return false;
    }

    NvComputer* leftComputer;
    NvComputer* rightComputer;
    NvApp leftNvApp;
    NvApp rightNvApp;
    if (leftHost.isEmpty() || rightHost.isEmpty() || leftHost == rightHost ||
        !hostAndApp(leftHost, leftApp, leftComputer, leftNvApp) ||
        !hostAndApp(rightHost, rightApp, rightComputer, rightNvApp)) {
        emit failed(tr("Choose two different online PCs and apps."));
        return false;
    }
    if (leftController.isEmpty() || rightController.isEmpty() || leftController == rightController) {
        emit failed(tr("Connect two different gamepads before starting couch co-op."));
        return false;
    }

    const QVariantList availableControllers = CoopControllers::enumerate();
    bool leftControllerFound = false;
    bool rightControllerFound = false;
    for (const QVariant& value : availableControllers) {
        const QString path = value.toMap().value("path").toString();
        leftControllerFound |= path == leftController;
        rightControllerFound |= path == rightController;
    }
    if (!leftControllerFound || !rightControllerFound) {
        emit failed(tr("One of the selected gamepads is no longer connected."));
        return false;
    }

    m_Window = window;
    m_WindowWasFullScreen = window->visibility() == QWindow::FullScreen;
    m_WindowWasMaximized = window->visibility() == QWindow::Maximized;
    window->showFullScreen();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    Display* display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        if (m_WindowWasFullScreen) {
            window->showFullScreen();
        }
        else if (m_WindowWasMaximized) {
            window->showMaximized();
        }
        else {
            window->showNormal();
        }
        m_Window = nullptr;
        emit failed(tr("Moonlight could not open the X11 display for couch co-op."));
        return false;
    }

    const WId parent = window->winId();
    XWindowAttributes attributes;
    if (!XGetWindowAttributes(display, parent, &attributes) || attributes.width <= 0 || attributes.height <= 0) {
        XCloseDisplay(display);
        clearPanes();
        emit failed(tr("Moonlight could not access its X11 window for couch co-op."));
        return false;
    }
    const int width = qMax(2, attributes.width);
    const int height = qMax(2, attributes.height);
    const Window root = DefaultRootWindow(display);
    const Window leftPane = XCreateSimpleWindow(display, parent, 0, 0, width / 2, height, 0,
                                                 BlackPixel(display, DefaultScreen(display)),
                                                 BlackPixel(display, DefaultScreen(display)));
    const Window rightPane = XCreateSimpleWindow(display, parent, width / 2, 0, width - width / 2, height, 0,
                                                  BlackPixel(display, DefaultScreen(display)),
                                                  BlackPixel(display, DefaultScreen(display)));
    Q_UNUSED(root);
    XSelectInput(display, leftPane, StructureNotifyMask);
    XSelectInput(display, rightPane, StructureNotifyMask);
    XMapWindow(display, leftPane);
    XMapWindow(display, rightPane);
    XFlush(display);
    XCloseDisplay(display);

    m_Active = true;
    m_Stopping = false;
    emit activeChanged();

    auto createWorker = [this](Window pane, const QString& host, int appId,
                               const QString& controller, const QString& description,
                               int paneWidth, int paneHeight) {
        Worker* worker = new Worker;
        worker->pane = pane;
        worker->description = description;
        worker->process = new QProcess(this);
        worker->process->setProcessChannelMode(QProcess::SeparateChannels);
        connect(worker->process, &QProcess::readyReadStandardOutput,
                this, &CouchCoop::workerOutputReady);
        connect(worker->process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                this, &CouchCoop::workerFinished);
        connect(worker->process, &QProcess::errorOccurred,
                this, &CouchCoop::workerError);
        worker->process->setProperty("couchCoopWorker", QVariant::fromValue<quintptr>(quintptr(worker)));
        const QStringList arguments{
            "coop-worker", host, QString::number(appId), QString::number(quintptr(pane)),
            QString::number(paneWidth), QString::number(paneHeight),
            controller
        };
        worker->process->start(QCoreApplication::applicationFilePath(), arguments);
        m_Workers.append(worker);
    };

    createWorker(leftPane, leftHost, leftApp, leftController, tr("left PC"), width / 2, height);
    createWorker(rightPane, rightHost, rightApp, rightController, tr("right PC"), width - width / 2, height);
    connect(window, &QWindow::widthChanged, this, &CouchCoop::updatePaneGeometry);
    connect(window, &QWindow::heightChanged, this, &CouchCoop::updatePaneGeometry);
    QTimer::singleShot(0, this, &CouchCoop::updatePaneGeometry);
    return true;
#endif
}

void CouchCoop::updatePaneGeometry()
{
#ifdef HAS_X11
    if (!m_Active || m_Window == nullptr) {
        return;
    }
    Display* display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        return;
    }
    XWindowAttributes attributes;
    if (!XGetWindowAttributes(display, m_Window->winId(), &attributes)) {
        XCloseDisplay(display);
        return;
    }
    const int width = qMax(2, attributes.width);
    const int height = qMax(2, attributes.height);
    for (int i = 0; i < m_Workers.size(); i++) {
        const int x = i == 0 ? 0 : width / 2;
        const int paneWidth = i == 0 ? width / 2 : width - width / 2;
        const Window pane = Window(m_Workers[i]->pane);
        XMoveResizeWindow(display, pane, x, 0, paneWidth, height);

        // The worker may have recreated its SDL window while resetting the
        // decoder. Resize whichever SDL child is currently attached to the
        // pane so a fullscreen/window geometry change reaches the renderer.
        Window root = 0;
        Window parent = 0;
        Window* children = nullptr;
        unsigned int childCount = 0;
        if (XQueryTree(display, pane, &root, &parent, &children, &childCount)) {
            Q_UNUSED(root);
            Q_UNUSED(parent);
            for (unsigned int childIndex = 0; childIndex < childCount; ++childIndex) {
                XMoveResizeWindow(display, children[childIndex], 0, 0,
                                  paneWidth, height);
            }
            if (children != nullptr) {
                XFree(children);
            }
        }
    }
    XFlush(display);
    XCloseDisplay(display);
#endif
}

void CouchCoop::workerOutputReady()
{
    QProcess* process = qobject_cast<QProcess*>(sender());
    if (process == nullptr) {
        return;
    }
    Worker* worker = reinterpret_cast<Worker*>(process->property("couchCoopWorker").value<quintptr>());
    if (worker == nullptr) {
        return;
    }
    worker->outputBuffer.append(process->readAllStandardOutput());
    int newlineIndex;
    while ((newlineIndex = worker->outputBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = worker->outputBuffer.left(newlineIndex).trimmed();
        worker->outputBuffer.remove(0, newlineIndex + 1);
        if (!line.isEmpty()) {
            handleWorkerLine(worker, line);
        }
    }
}

void CouchCoop::handleWorkerLine(Worker* worker, const QByteArray& line)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return;
    }
    const QJsonObject object = document.object();
    if (object.value("ready").toBool()) {
        worker->ready = true;
    }
    if (object.contains("error") && !m_Stopping) {
        failAndStop(tr("Couch co-op could not start %1: %2").arg(worker->description, object.value("error").toString()));
    }
}

void CouchCoop::workerFinished(int exitCode, QProcess::ExitStatus status)
{
    QProcess* process = qobject_cast<QProcess*>(sender());
    if (process == nullptr) {
        return;
    }
    Worker* worker = reinterpret_cast<Worker*>(process->property("couchCoopWorker").value<quintptr>());
    if (worker == nullptr) {
        return;
    }
    worker->finished = true;
    if (!m_Stopping) {
        if (status != QProcess::NormalExit || exitCode != 0) {
            const QString error = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
            failAndStop(error.isEmpty() ? tr("A couch co-op stream stopped unexpectedly.") : error);
        }
        else {
            // A normal disconnect from either host ends the paired layout.
            stop();
        }
    }
    bool allFinished = !m_Workers.isEmpty();
    for (Worker* current : std::as_const(m_Workers)) {
        allFinished &= current->finished;
    }
    if (allFinished) {
        clearPanes();
        m_Active = false;
        m_Stopping = false;
        emit activeChanged();
    }
}

void CouchCoop::workerError(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart) {
        QProcess* process = qobject_cast<QProcess*>(sender());
        if (process != nullptr) {
            Worker* worker = reinterpret_cast<Worker*>(process->property("couchCoopWorker").value<quintptr>());
            if (worker != nullptr) {
                worker->finished = true;
            }
        }
        failAndStop(tr("Moonlight could not start a couch co-op worker."));

        bool allFinished = !m_Workers.isEmpty();
        for (Worker* worker : std::as_const(m_Workers)) {
            allFinished &= worker->finished;
        }
        if (allFinished) {
            clearPanes();
            m_Active = false;
            m_Stopping = false;
            emit activeChanged();
        }
    }
}

void CouchCoop::failAndStop(const QString& message)
{
    if (!message.isEmpty()) {
        emit failed(message);
    }
    stop();
}

void CouchCoop::clearPanes()
{
#ifdef HAS_X11
    if (m_Window != nullptr) {
        Display* display = XOpenDisplay(nullptr);
        if (display != nullptr) {
            for (Worker* worker : std::as_const(m_Workers)) {
                if (worker->pane != 0) {
                    XDestroyWindow(display, Window(worker->pane));
                }
            }
            XFlush(display);
            XCloseDisplay(display);
        }
    }
#endif
    for (Worker* worker : std::as_const(m_Workers)) {
        if (worker->process != nullptr) {
            worker->process->disconnect(this);
            worker->process->deleteLater();
        }
        delete worker;
    }
    m_Workers.clear();
    if (m_Window != nullptr) {
        disconnect(m_Window, nullptr, this, nullptr);
        if (m_WindowWasFullScreen) {
            m_Window->showFullScreen();
        }
        else if (m_WindowWasMaximized) {
            m_Window->showMaximized();
        }
        else {
            m_Window->showNormal();
        }
    }
    m_Window = nullptr;
}

void CouchCoop::stop()
{
    if (!m_Active && m_Workers.isEmpty()) {
        return;
    }
    m_Stopping = true;
    for (Worker* worker : std::as_const(m_Workers)) {
        if (worker->process != nullptr && worker->process->state() != QProcess::NotRunning) {
            worker->process->terminate();
        }
    }
    QTimer::singleShot(1500, this, [this]() {
        for (Worker* worker : std::as_const(m_Workers)) {
            if (worker->process != nullptr && worker->process->state() != QProcess::NotRunning) {
                worker->process->kill();
            }
        }
    });
    if (m_Workers.isEmpty()) {
        clearPanes();
        m_Active = false;
        m_Stopping = false;
        emit activeChanged();
    }
}

