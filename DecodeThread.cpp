#include "DecodeThread.h"
#include <QDebug>

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

void DecodeThread::run()
{
    qDebug() << "[DecodeThread] 解码线程已启动";

    // 初始化解码器
    if (!m_decoder.isInitialized()) {
        if (!m_decoder.init(AV_CODEC_ID_H264)) {
            qCritical() << "[DecodeThread] 视频解码器初始化失败";
            return;
        }
    }

    QByteArray data;
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
    }

    qDebug() << "[DecodeThread] 解码线程已退出";
}
