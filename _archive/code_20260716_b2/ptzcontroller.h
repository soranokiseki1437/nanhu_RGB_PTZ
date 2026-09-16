#ifndef PTZCONTROLLER_H
#define PTZCONTROLLER_H

#include <QObject>
#include <QSerialPort>
#include <QTimer>

// 修复P3#25: 默认波特率常量
constexpr int DEFAULT_PTZ_BAUDRATE = 9600;

class PTZController : public QObject
{
    Q_OBJECT
public:
    explicit PTZController(const QString &portName, int baudRate = DEFAULT_PTZ_BAUDRATE, uint8_t address = 1, QObject *parent = nullptr);
    ~PTZController();

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

signals:
    // 信号：当收到云台角度数据时触发
    void angleReceived(float pan, float tilt);
    //Hex 数据监控信号的声明
    void rawDataInout(bool isTx, QByteArray data);

private slots:
    void onReadyRead(); // 处理接收到的串口数据
    void onSerialError(QSerialPort::SerialPortError error);
    void onAutoQueryTimer();  // 定时查询定时器回调

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

    QByteArray m_buffer;
    
    // 定时查询相关
    QTimer *m_autoQueryTimer;
    bool m_autoQuerying;

    // 内部辅助函数
    QByteArray buildPelcoDPacket(uint8_t cmd1, uint8_t cmd2, uint8_t data1, uint8_t data2);
    uint8_t calculateChecksum(const QByteArray &data);

    // 底层发送接口，方便做串口状态检查
    void writeToSerial(const QByteArray &packet);
};

#endif // PTZCONTROLLER_H
