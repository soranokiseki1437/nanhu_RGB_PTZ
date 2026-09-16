#include "videodecoder.h"
#include <QDebug>

VideoDecoder::VideoDecoder()
    : m_codec(nullptr)
    , m_codecContext(nullptr)
    , m_frame(nullptr)
    , m_packet(nullptr)
    , m_swsContext(nullptr)
    , m_width(0)
    , m_height(0)
    , m_initialized(false)
{
    m_rgbBuffer[0] = nullptr;
}

VideoDecoder::~VideoDecoder()
{
    cleanup();
}

bool VideoDecoder::init(AVCodecID codecId)
{
    if (m_initialized) {
        cleanup();
    }

    m_codec = avcodec_find_decoder(codecId);
    if (!m_codec) {
        qCritical() << "Failed to find decoder for codec ID:" << codecId;
        return false;
    }

    m_codecContext = avcodec_alloc_context3(m_codec);
    if (!m_codecContext) {
        qCritical() << "Failed to allocate codec context";
        return false;
    }

    if (avcodec_open2(m_codecContext, m_codec, nullptr) < 0) {
        qCritical() << "Failed to open codec";
        cleanup();
        return false;
    }

    m_frame = av_frame_alloc();
    m_packet = av_packet_alloc();

    if (!m_frame || !m_packet) {
        qCritical() << "Failed to allocate frame or packet";
        cleanup();
        return false;
    }

    m_initialized = true;
    qDebug() << "Video decoder initialized successfully for codec:" << codecId;
    return true;
}

void VideoDecoder::cleanup()
{
    if (m_swsContext) {
        sws_freeContext(m_swsContext);
        m_swsContext = nullptr;
    }

    if (m_rgbBuffer[0]) {
        av_freep(&m_rgbBuffer[0]);
        m_rgbBuffer[0] = nullptr;
    }

    if (m_frame) {
        av_frame_free(&m_frame);
        m_frame = nullptr;
    }

    if (m_packet) {
        av_packet_free(&m_packet);
        m_packet = nullptr;
    }

    if (m_codecContext) {
        avcodec_free_context(&m_codecContext);
        m_codecContext = nullptr;
    }

    m_codec = nullptr;
    m_width = 0;
    m_height = 0;
    m_initialized = false;
}

QImage VideoDecoder::decodeFrame(const uint8_t* data, int size)
{
    if (!m_initialized || !m_codecContext || !m_frame || !m_packet) {
        return QImage();
    }

    m_packet->data = const_cast<uint8_t*>(data);
    m_packet->size = size;

    int sendResult = avcodec_send_packet(m_codecContext, m_packet);
    if (sendResult < 0) {
        return QImage();
    }

    int receiveResult = avcodec_receive_frame(m_codecContext, m_frame);
    if (receiveResult < 0) {
        return QImage();
    }

    if (m_width != m_frame->width || m_height != m_frame->height) {
        m_width = m_frame->width;
        m_height = m_frame->height;

        if (m_swsContext) {
            sws_freeContext(m_swsContext);
            m_swsContext = nullptr;
        }

        if (m_rgbBuffer[0]) {
            av_freep(&m_rgbBuffer[0]);
            m_rgbBuffer[0] = nullptr;
        }

        m_swsContext = sws_getContext(
            m_width, m_height, (AVPixelFormat)m_frame->format,
            m_width, m_height, AV_PIX_FMT_RGB24,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        );

        av_image_alloc(m_rgbBuffer, m_rgbLinesize, m_width, m_height, AV_PIX_FMT_RGB24, 1);
    }

    if (m_swsContext && m_rgbBuffer[0]) {
        sws_scale(
            m_swsContext,
            m_frame->data,
            m_frame->linesize,
            0,
            m_height,
            m_rgbBuffer,
            m_rgbLinesize
        );

        QImage image(m_rgbBuffer[0], m_width, m_height, m_rgbLinesize[0], QImage::Format_RGB888);
        return image.copy();
    }

    return QImage();
}
