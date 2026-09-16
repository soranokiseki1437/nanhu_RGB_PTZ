#ifndef VIDEODECODER_H
#define VIDEODECODER_H

#include <QImage>
#include <QString>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}

class VideoDecoder
{
public:
    VideoDecoder();
    ~VideoDecoder();

    bool init(AVCodecID codecId);
    void cleanup();
    QImage decodeFrame(const uint8_t* data, int size);
    bool isInitialized() const { return m_codecContext != nullptr; }

private:
    const AVCodec* m_codec;
    AVCodecContext* m_codecContext;
    AVFrame* m_frame;
    AVPacket* m_packet;
    SwsContext* m_swsContext;
    uint8_t* m_rgbBuffer[4];
    int m_rgbLinesize[4];
    int m_width;
    int m_height;
    bool m_initialized;
};

#endif // VIDEODECODER_H
