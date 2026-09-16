#ifndef RECORDTHREAD_H
#define RECORDTHREAD_H

#include <QThread>
#include <QMutex>
#include "ThreadSafeQueue.h"
#include "videodecoder.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

class RecordThread : public QThread
{
    Q_OBJECT

public:
    explicit RecordThread(QObject *parent = nullptr);
    ~RecordThread();

    bool initRecord(const QString& filePath, int frameRate);
    void pushData(const QByteArray &data);
    void stopRecord();

signals:
    void errorOccurred(const QString &error);
    void recordFinished(const QString &filePath);

protected:
    void run() override;

private:
    void cleanup();
    bool writeVideoFrame(AVFrame* frame);
    bool initEncoder(int width, int height);

    ThreadSafeQueue<QByteArray> m_dataQueue;
    QString m_filePath;
    bool m_running;
    bool m_initialized;
    int m_frameRate;
    int m_width;
    int m_height;
    int64_t m_pts;
    int m_encoderFailCount;  // 编码器初始化连续失败计数

    // 解码
    VideoDecoder m_decoder;

    // FFmpeg 相关 - 编码和封装
    AVFormatContext* m_formatContext;
    AVStream* m_videoStream;
    const AVCodec* m_encoder;
    AVCodecContext* m_encoderCtx;
    AVPacket* m_packet;
    SwsContext* m_swsCtx;
    AVFrame* m_yuvFrame;
    uint8_t* m_yuvBuffer[4];
    int m_yuvLinesize[4];
};

#endif // RECORDTHREAD_H
