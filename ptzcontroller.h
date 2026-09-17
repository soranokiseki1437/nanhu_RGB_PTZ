#ifndef PTZCONTROLLER_H
#define PTZCONTROLLER_H

#include <QObject>
#include <QSerialPort>
#include <QTimer>
#include <QQueue>
#include <QByteArray>
#include <atomic>

// 修复P3#25: 默认波特率常量
constexpr int DEFAULT_PTZ_BAUDRATE = 9600;

class PTZController : public QObject
{
    Q_OBJECT
public:
    explicit PTZController(const QString &portName, int baudRate = DEFAULT_PTZ_BAUDRATE, uint8_t address = 1, QObject *parent = nullptr);
    ~PTZController();

    // 角度安全限位：越界目标值会被夹紧，防止球机堵转（按实际球机参数调整）
    static constexpr float PAN_MIN  = 0.0f;
    static constexpr float PAN_MAX  = 360.0f;
    static constexpr float TILT_MIN = -40.0f;
    static constexpr float TILT_MAX = 40.0f;

    // 获取串口打开状态
    bool isOpen() const;

    // 获取当前角度
    float getCurrentPan() const { return m_currentPan; }
    float getCurrentTilt() const { return m_currentTilt; }
    
    // 获取当前速度
    float getCurrentPanSpeed() const { return m_currentPanSpeed; }
    float getCurrentTiltSpeed() const { return m_currentTiltSpeed; }

public slots:
    // --- 初始化函数 --- 在对象移动到线程后调用
    void init();
    
    // --- 核心控制函数 --- 改为槽函数，供跨线程调用
    void moveTo(float pan, float tilt, float speed = 0.5f);
    void stop();
    void queryAngle();
    void moveDirection(uint8_t cmd2, uint8_t hSpeed, uint8_t vSpeed);

    // --- 预置位功能 ---
    void callPreset(uint8_t presetId);
    void setPreset(uint8_t presetId);
    
    // --- 定时查询功能 ---
    void startAutoQuery(int intervalMs = 100);  // 默认10Hz
    void stopAutoQuery();
    bool isAutoQuerying() const;
    void prepareForDestruction();  // 在线程退出前关闭串口，确保线程安全

signals:
    // 信号：当收到云台角度数据时触发
    void angleReceived(float pan, float tilt);
    //Hex 数据监控信号的声明
    void rawDataInout(bool isTx, QByteArray data);
    void serialError(const QString &errorString);  // L3: 串口错误通知上层

private slots:
    void onReadyRead(); // 处理接收到的串口数据
    void onSerialError(QSerialPort::SerialPortError error);
    void onAutoQueryTimer();  // 定时查询定时器回调
    void onCmdTimerTimeout();      // 命令队列出队发送（10ms 节拍）
    void onStopWatchdogTimeout();  // 连续运动失联保护：超时补发 stop
    void onReconnectTimeout();     // 串口打开失败后自动重连

private:
    uint8_t m_address;
    QString m_portName;
    int m_baudRate;
    QSerialPort *m_serial;

    // 缓存当前角度，确保发送信号时是一个完整的坐标对
    float m_currentPan = 0.0f;
    float m_currentTilt = 0.0f;
    
    // 缓存当前速度
    float m_currentPanSpeed = 0.0f;
    float m_currentTiltSpeed = 0.0f;

    bool m_positioning = false;     // 是否有绝对定位在途（moveTo 置位，stop 清零）
    bool m_noPortWarned = false;    // 串口未打开时只告警一次，避免刷日志

    QByteArray m_buffer;

    // 命令队列：moveTo/stop 的多包序列按 10ms 节拍逐条发出，stop 可整体作废
    QQueue<QByteArray> m_cmdQueue;
    QTimer *m_cmdTimer = nullptr;
    // 连续运动失联保护（single-shot，仅在 moveDirection 时重置）
    QTimer *m_stopWatchdog = nullptr;
    // 串口打开失败后的自动重连（2s）
    QTimer *m_reconnectTimer = nullptr;
    
    // 定时查询相关
    QTimer *m_autoQueryTimer;
    std::atomic<bool> m_autoQuerying;

    // 内部辅助函数
    QByteArray buildPelcoDPacket(uint8_t cmd1, uint8_t cmd2, uint8_t data1, uint8_t data2);
    uint8_t calculateChecksum(const QByteArray &data);
    void openSerialPort();  // 打开串口（init 与重连定时器共用）

    // 底层发送接口，方便做串口状态检查
    void writeToSerial(const QByteArray &packet);
};

#endif // PTZCONTROLLER_H