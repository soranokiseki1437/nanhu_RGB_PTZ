#include "DecodeThread.h"
#include <QDebug>
#include <QElapsedTimer>

DecodeThread::DecodeThread(QObject *parent) : QThread(parent), m_running(true)
{
    m_dataQueue.setMaxSize(50);
}

DecodeThread::~DecodeThread()
{
    stop();
    wait();
}

void DecodeThread::pushData(const QByteArray &data)
{
    m_dataQueue.push(data);
}

void DecodeThread::stop()
{
    m_running = false;
    m_dataQueue.stop();
}

void DecodeThread::restart()
{
    m_dataQueue.reset();  // 清空残留并复位 stopped 标志，否则复活后 push 丢数据/waitAndPop 恒失败
    if (!isRunning()) {
        m_running = true;
        start();
    }
}

void DecodeThread::run()
{
    qDebug() << "[DecodeThread] 解码线程已启动";

    // 初始化解码器：按相机实际编码类型（H.264/H.265/MJPEG），
    // 每次进入线程都重建，保证切换编码类型后仍能解码
    if (!m_decoder.init(VideoDecoder::codecIdFromEncType(m_encType))) {
        qCritical() << "[DecodeThread] 视频解码器初始化失败，encType =" << m_encType;
        return;
    }

    QByteArray data;
    QElapsedTimer statsTimer;
    statsTimer.start();
    while (m_running.load()) {
        // 等待队列数据
        if (m_dataQueue.waitAndPop(data, 1000)) {
            try {
                QImage frame = m_decoder.decodeFrame(
                    reinterpret_cast<uint8_t*>(data.data()),
                    data.size()
                );

                if (!frame.isNull()) {
                    emit frameDecoded(frame);
                }
            } catch (...) {
                qDebug() << "[DecodeThread] 解码异常，跳过当前帧";
            }
        }

        // 每 5s 汇总一次丢帧，便于评估跟踪输入质量（队列满会丢最旧帧）
        if (statsTimer.elapsed() >= 5000) {
            const int dropped = m_dataQueue.droppedCount();
            if (dropped > 0) {
                qWarning() << "[DecodeThread] 解码队列积压，最近 5s 丢弃最旧帧:" << dropped;
                m_dataQueue.resetDropped();
            }
            statsTimer.restart();
        }
    }

    qDebug() << "[DecodeThread] 解码线程已退出";
}
