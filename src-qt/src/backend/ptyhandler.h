#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QProcess>
#include <QProcessEnvironment>

class QSocketNotifier;
class QTimer;

class PtyHandler : public QObject {
    Q_OBJECT

public:
    explicit PtyHandler(QObject *parent = nullptr);
    ~PtyHandler() override;

    bool start(const QString &program, const QStringList &arguments,
               const QString &workingDir = {},
               const QProcessEnvironment &environment = QProcessEnvironment::systemEnvironment());
    void write(const QByteArray &data);
    void kill();
    bool isRunning() const;
    QString errorString() const;

signals:
    void outputReceived(const QByteArray &data);
    void finished(int exitCode);
    void errorOccurred(QProcess::ProcessError error);

private:
#if defined(Q_OS_UNIX) && !defined(Q_OS_ANDROID)
    void readFromPty();
    void pollChild();
    void closePty();

    int m_masterFd = -1;
    qint64 m_childPid = -1;
    QSocketNotifier *m_notifier = nullptr;
    QTimer *m_childPoll = nullptr;
#else
    void onReadyReadStandardOutput();
    void onReadyReadStandardError();
    void onFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onErrorOccurred(QProcess::ProcessError error);

    QProcess *m_process = nullptr;
#endif
    QString m_error;
};
