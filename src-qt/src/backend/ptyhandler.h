#pragma once

#include <QObject>
#include <QString>
#include <QProcess>
#include <QByteArray>

class PtyHandler : public QObject {
    Q_OBJECT

public:
    explicit PtyHandler(QObject *parent = nullptr);
    ~PtyHandler() override;

    bool start(const QString &program, const QStringList &arguments, const QString &workingDir = {});
    void write(const QByteArray &data);
    void kill();
    bool isRunning() const;
    QString errorString() const;

signals:
    void outputReceived(const QByteArray &data);
    void finished(int exitCode);
    void errorOccurred(QProcess::ProcessError error);

private slots:
    void onReadyReadStandardOutput();
    void onReadyReadStandardError();
    void onFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onErrorOccurred(QProcess::ProcessError error);

private:
    QProcess *m_process = nullptr;
    QString m_error;
};
