#pragma once

#include <QObject>
#include <QByteArray>
#include <QProcess>
#include <QQuickWindow>
#include <QVariantList>
#include <QVariantMap>

class ComputerManager;
class QTimer;
class NvComputer;
class NvApp;

class CouchCoop : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool supported READ supported CONSTANT)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(QVariantList hosts READ hosts NOTIFY hostsChanged)
    Q_PROPERTY(QVariantList controllers READ controllers NOTIFY controllersChanged)

public:
    explicit CouchCoop(QObject* parent = nullptr);
    ~CouchCoop() override;

    bool supported() const;
    bool active() const;
    QVariantList hosts() const;
    QVariantList controllers() const;

    Q_INVOKABLE void initialize(ComputerManager* manager);
    Q_INVOKABLE bool start(QQuickWindow* window,
                           const QString& leftHost, int leftApp, const QString& leftController,
                           const QString& rightHost, int rightApp, const QString& rightController);
    Q_INVOKABLE void stop();

signals:
    void activeChanged();
    void hostsChanged();
    void controllersChanged();
    void failed(QString message);

private slots:
    void refreshHosts();
    void refreshControllers();
    void workerOutputReady();
    void workerFinished(int exitCode, QProcess::ExitStatus status);
    void workerError(QProcess::ProcessError error);

private:
    struct Worker {
        QProcess* process = nullptr;
        quintptr pane = 0;
        bool ready = false;
        bool finished = false;
        QString description;
        QByteArray outputBuffer;
    };

    void clearPanes();
    void updatePaneGeometry();
    void failAndStop(const QString& message);
    void handleWorkerLine(Worker* worker, const QByteArray& line);
    bool hostAndApp(const QString& uuid, int appId, NvComputer*& computer, NvApp& app) const;

    ComputerManager* m_Manager = nullptr;
    QVariantList m_Hosts;
    QVariantList m_Controllers;
    QTimer* m_ControllerRefreshTimer = nullptr;
    QList<Worker*> m_Workers;
    QQuickWindow* m_Window = nullptr;
    bool m_Active = false;
    bool m_Stopping = false;
    bool m_WindowWasFullScreen = false;
    bool m_WindowWasMaximized = false;
};

