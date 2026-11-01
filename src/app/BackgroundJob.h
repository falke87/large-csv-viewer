// Runs one long task (filter, sort, export, ...) on a worker thread so the window stays
// responsive. The UI reads `done` for the progress bar and can set `cancel`.
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <atomic>
#include <functional>
#include <thread>

class BackgroundJob : public QObject
{
    Q_OBJECT
public:
    using Work = std::function<void(BackgroundJob &job)>;

    BackgroundJob(const QString &title, qint64 total, Work work, QObject *parent = nullptr);
    ~BackgroundJob() override;    // cancels and waits

    void start();
    void requestCancel() { cancel = true; }

    QString title() const { return m_title; }
    qint64 total() const { return m_total; }
    int permille() const;         // 0..1000
    double seconds() const { return m_seconds; }
    bool isCancelled() const { return cancel; }

    std::atomic<qint64> done{0};
    std::atomic<bool> cancel{false};

signals:
    void finished();

private:
    QString m_title;
    qint64 m_total;
    Work m_work;
    std::thread m_thread;
    QElapsedTimer m_timer;
    double m_seconds = 0;
};
