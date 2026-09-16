#ifndef DATARECORDER_H
#define DATARECORDER_H

#include <QObject>
#include <QTimer>
#include <QMutex>
#include <QVector>
#include <QDateTime>
#include "datatypes.h"

class DataRecorder : public QObject
{
    Q_OBJECT

public:
    explicit DataRecorder(QObject *parent = nullptr);
    ~DataRecorder();

    // 开始/停止记录
    bool startRecording(const QString &baseFilePath);
    void stopRecording();
    bool isRecording() const;

    // 数据录入接口
    void updatePTZData(float pan, float tilt, float panSpeed = 0, float tiltSpeed = 0);
    void updateDetectionData(float targetX, float targetY, float offsetX, float offsetY, float confidence);
    void recordFrame(int frameIndex);

    // 获取数据
    QVector<DataPoint> getDataPoints() const;
    int getDataPointCount() const;

    // 导出数据
    bool exportToCsv(const QString &filePath);
    bool exportToExcel(const QString &filePath);

signals:
    void recordingStarted(const QString &filePath);
    void recordingStopped(const QString &filePath);
    void errorOccurred(const QString &error);
    void dataPointAdded(const DataPoint &point);

private slots:
    void onRecordTimer();

private:
    mutable QMutex m_mutex;
    QTimer *m_recordTimer;
    QVector<DataPoint> m_dataPoints;
    
    bool m_recording;
    qint64 m_startTimeMs;
    QString m_baseFilePath;
    QString m_csvFilePath;
    
    // 当前状态缓存
    PTZData m_currentPTZ;
    DetectionData m_currentDetection;
    int m_frameCounter;
    int m_recordIntervalMs;  // 记录间隔（毫秒），默认100ms(10Hz)
};

#endif // DATARECORDER_H
