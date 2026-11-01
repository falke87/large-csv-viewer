#include "BackgroundJob.h"

#include <algorithm>

BackgroundJob::BackgroundJob(const QString &title, qint64 total, Work work, QObject *parent)
    : QObject(parent), m_title(title), m_total(std::max<qint64>(1, total)), m_work(std::move(work))
{
}

BackgroundJob::~BackgroundJob()
{
    cancel = true;
    if (m_thread.joinable()) m_thread.join();
}

void BackgroundJob::start()
{
    m_timer.start();
    m_thread = std::thread([this] {
        m_work(*this);
        // Back on the UI thread: join the worker and report.
        QMetaObject::invokeMethod(this, [this] {
            if (m_thread.joinable()) m_thread.join();
            m_seconds = m_timer.nsecsElapsed() / 1e9;
            emit finished();
        }, Qt::QueuedConnection);
    });
}

int BackgroundJob::permille() const
{
    return int(std::clamp<qint64>(done.load() * 1000 / m_total, 0, 1000));
}
