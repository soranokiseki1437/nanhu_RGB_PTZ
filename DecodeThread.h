#ifndef DECODETHREAD_H
#define DECODETHREAD_H

#include <QThread>
#include <QImage>
#include <atomic>
#include "ThreadSafeQueue.h"
#include "videodecoder.h"

class DecodeThread : public QThread
{
    Q_OBJECT

public:
    explicit DecodeThread(QObject *parent = nullptr);
    ~DecodeThread();

    void pushData(const QByteArray &data);
    void stop();

signals:
    void frameDecoded(const QImage &frame);

protected:
    void run() override;

private:
    ThreadSafeQueue<QByteArray> m_dataQueue;
    VideoDecoder m_decoder;
    std::atomic<bool> m_running;
};

#endif // DECODETHREAD_H
