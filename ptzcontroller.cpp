// 修复P3#25: 提取波特率为常量（在头文件中定义）
#include "ptzcontroller.h"
#include <QDebug>
#include <QThread>
#include <QTimer>
#include <QMutex>

// 1. 构造函数：只保存配置，不打开串口
PTZController::PTZController(const QString &portName, int baudRate, uint8_t address, QObject *parent)
    : QObject(parent), m_address(address), m_portName(portName), m_baudRate(baudRate), m_serial(nullptr),
      m_currentPanSpeed(0.0f), m_currentTiltSpeed(0.0f),
      m_autoQueryTimer(new QTimer(this)), m_autoQuerying(false)
{
    connect(m_autoQueryTimer, &QTimer::timeout, this, &PTZController::onAutoQueryTimer);
}

// 2. init() 函数：在对象移动到线程后调用，真正打开串口
void PTZController::init()
{
    // 在子线程中创建串口实例
    m_serial = new QSerialPort(this);

    // 配置串口参数
    m_serial->setPortName(m_portName);
    m_serial->setBaudRate(m_baudRate);
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setParity(QSerialPort::NoParity);
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setFlowControl(QSerialPort::NoFlowControl);

    connect(m_serial, &QSerialPort::readyRead, this, &PTZController::onReadyRead);
    connect(m_serial, &QSerialPort::errorOccurred, this, &PTZController::onSerialError);

    // 尝试打开串口
    if (m_serial->open(QIODevice::ReadWrite)) {
        qDebug() << "[成功] 串口已成功打开 ->" << m_portName;
    } else {
        qDebug() << "[错误] 串口打开失败:" << m_serial->errorString();
    }
}

PTZController::~PTZController()
{
    stopAutoQuery();
    if (m_serial && m_serial->isOpen()) {
        m_serial->close();
    }
}

void PTZController::prepareForDestruction()
{
    // 在 PTZ 线程中执行，确保串口在本线程关闭，避免跨线程析构
    stopAutoQuery();
    if (m_serial && m_serial->isOpen()) {
        m_serial->close();
    }
}

bool PTZController::isOpen() const
{
    return m_serial && m_serial->isOpen();
}

void PTZController::onSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError) return;
    qWarning() << "[PTZController] 串口错误:" << m_serial->errorString();
    emit serialError(m_serial->errorString());
}

// 校验和计算
uint8_t PTZController::calculateChecksum(const QByteArray &data)
{
    int sum = 0;
    for (char byte : data) {
        sum += static_cast<uint8_t>(byte);
    }
    return sum & 0xFF;
}

// --- 纯粹的"打包工"：只负责生成 Pelco-D 协议数据包 ---
QByteArray PTZController::buildPelcoDPacket(uint8_t cmd1, uint8_t cmd2, uint8_t data1, uint8_t data2)
{
    QByteArray packet;

    // 组包: 地址 + Cmd1 + Cmd2 + Data1 + Data2
    packet.append(static_cast<char>(m_address));
    packet.append(static_cast<char>(cmd1));
    packet.append(static_cast<char>(cmd2));
    packet.append(static_cast<char>(data1));
    packet.append(static_cast<char>(data2));

    // 计算校验码 [cite: 5]
    uint8_t checksum = calculateChecksum(packet);

    // 添加帧头 0xFF 和 校验码
    packet.prepend(static_cast<char>(0xFF));
    packet.append(static_cast<char>(checksum));

    return packet; // 返回拼接好的字节数组，不进行任何 I/O 操作
}

// --- 统一的"快递员"：只负责将数据写入串口 ---
void PTZController::writeToSerial(const QByteArray &packet)
{
    if (m_serial && m_serial->isOpen()) {
        m_serial->write(packet);
        emit rawDataInout(true, packet); // 发送数据信号
        // qDebug() << "[发送数据]" << packet.toHex().toUpper();
    } else {
        qDebug() << "[警告] 串口未打开，数据被丢弃！";
    }
}

// 绝对定位控制
void PTZController::moveTo(float pan, float tilt, float speed)
{
    // --- 第一步：设置定位速度 (指令 0x5F) ---
    uint8_t speedByte;
    if (speed <= 1.0f)
        speedByte = static_cast<uint8_t>(speed * 63);
    else
        speedByte = static_cast<uint8_t>(speed);

    if (speedByte > 0x3F) speedByte = 0x3F;

    // 立刻发送速度设置指令
    writeToSerial(buildPelcoDPacket(0x00, 0x5F, speedByte, speedByte));

    // --- 第二步：发送水平角度定位 (指令 0x4B) ---
    int pa = static_cast<int>(pan * 100);
    QByteArray panCmd = buildPelcoDPacket(0x00, 0x4B,
                                          static_cast<uint8_t>(pa >> 8),
                                          static_cast<uint8_t>(pa & 0xFF));
    // 利用 QTimer 延时 20ms 发送，不阻塞主线程
    QTimer::singleShot(20, this, [this, panCmd]() {
        writeToSerial(panCmd);
    });

    // --- 第三步：发送俯仰角度定位 (指令 0x4D) ---
    float protocolTilt = tilt;
    if (tilt < 0.0f) {
        protocolTilt = 360.0f + tilt;
    }

    int ta = static_cast<int>(protocolTilt * 100.0f);
    QByteArray tiltCmd = buildPelcoDPacket(0x00, 0x4D,
                                           static_cast<uint8_t>(ta >> 8),
                                           static_cast<uint8_t>(ta & 0xFF));
    // 利用 QTimer 延时 70ms 发送
    QTimer::singleShot(70, this, [this, tiltCmd]() {
        writeToSerial(tiltCmd);
    });

    qDebug() << "[发送] 移动指令 -> P:" << pan << " T:" << tilt
             << " (协议映射值:" << protocolTilt << ") Spd:" << speedByte;
}

// 停止指令
void PTZController::stop()
{
    writeToSerial(buildPelcoDPacket(0x00, 0x00, 0x00, 0x00));
    m_currentPanSpeed = 0.0f;
    m_currentTiltSpeed = 0.0f;
    m_buffer.clear();
    qDebug() << "[发送] 停止指令";
}

// 查询角度
void PTZController::queryAngle()
{
    QByteArray panCmd = buildPelcoDPacket(0x00, 0x51, 0x00, 0x00);
    writeToSerial(panCmd);

    QByteArray tiltCmd = buildPelcoDPacket(0x00, 0x53, 0x00, 0x00);
    // 延时 50ms 后发送垂直查询
    QTimer::singleShot(50, this, [this, tiltCmd]() {
        writeToSerial(tiltCmd);
    });
}

// 调用预置位
void PTZController::callPreset(uint8_t presetId)
{
    // 调用底层发送接口，传入组装好的 Pelco-D 预置位调用指令包
    writeToSerial(buildPelcoDPacket(0x00, 0x07, 0x00, presetId));
}

// 设置预置位
void PTZController::setPreset(uint8_t presetId)
{
    // 调用底层发送接口，传入组装好的 Pelco-D 预置位设置指令包
    writeToSerial(buildPelcoDPacket(0x00, 0x03, 0x00, presetId));
}

// 串口数据接收处理
void PTZController::onReadyRead()
{
    QByteArray data = m_serial->readAll();
    m_buffer.append(data);
    emit rawDataInout(false, data); // 接收数据信号

    while (m_buffer.size() > 0) {
        int headerIndex = m_buffer.indexOf(static_cast<char>(0xFF));

        if (headerIndex == -1) {
            m_buffer.clear();
            return;
        }
        if (headerIndex > 0) {
            m_buffer.remove(0, headerIndex);
        }

        int packetLen = 7;
        if (m_buffer.size() < packetLen) {
            return; // 凑够7个字节再处理
        }

        QByteArray packet = m_buffer.left(packetLen);

        // --- 新增：1. 校验地址 ---
        if (static_cast<uint8_t>(packet.at(1)) != m_address) {
            m_buffer.remove(0, 1); // 丢弃当前的 0xFF，继续向后找
            continue;
        }

        // --- 新增：2. 校验 Checksum ---
        // Pelco-D: (Byte2 + Byte3 + Byte4 + Byte5 + Byte6) & 0xFF
        uint8_t calcSum = calculateChecksum(packet.mid(1, 5));
        uint8_t recvSum = static_cast<uint8_t>(packet.at(6));

        if (calcSum != recvSum) {
            qDebug() << "[警告] 串口数据校验失败，丢弃错包！";
            m_buffer.remove(0, 1);
            continue;
        }

        // --- 解析正确的数据 ---
        uint8_t cmd2 = static_cast<uint8_t>(packet.at(3));
        uint8_t high = static_cast<uint8_t>(packet.at(4));
        uint8_t low = static_cast<uint8_t>(packet.at(5));

        // 协议公式：角度 = (DataH * 256 + DataL) / 100 [cite: 159, 161]
        double angle = ((high << 8) + low) / 100.0;

        if (cmd2 == 0x59) {
            // 水平角度回传，直接赋值
            m_currentPan = angle;
            emit angleReceived(m_currentPan, m_currentTilt);
        } else if (cmd2 == 0x5B) {
            // 俯仰角度回传
            // 【核心修复】：因为物理限位最大只有 40°，所以如果回传的角度非常大（比如大于270°）
            // 必然是因为云台处于抬头状态（360递减）
            if (angle > 270.0) {
                // 还原为直观的负角度，例如 349.5 - 360.0 = -10.5°
                m_currentTilt = angle - 360.0;
            } else {
                m_currentTilt = angle;
            }
            emit angleReceived(m_currentPan, m_currentTilt);
        }

        m_buffer.remove(0, packetLen);
    }
}
// 实时方向盘控制
void PTZController::moveDirection(uint8_t cmd2, uint8_t hSpeed, uint8_t vSpeed)
{
    // 强制安全限幅：根据 Pelco-D 手册，最大速度不能超过 0x3F (63)
    if (hSpeed > 0x3F) hSpeed = 0x3F;
    if (vSpeed > 0x3F) vSpeed = 0x3F;

    // 更新当前速度
    m_currentPanSpeed = hSpeed;
    m_currentTiltSpeed = vSpeed;

    // 组包：Cmd1固定0x00，Data1是水平速度，Data2是垂直速度
    QByteArray packet = buildPelcoDPacket(0x00, cmd2, hSpeed, vSpeed);

    // 直接发送到底层串口
    writeToSerial(packet);
}

// --- 定时查询功能实现 ---
void PTZController::startAutoQuery(int intervalMs)
{
    if (m_autoQuerying) {
        qWarning() << "[PTZController] 已经在自动查询中";
        return;
    }
    
    m_autoQuerying = true;
    m_autoQueryTimer->start(intervalMs);
    qDebug() << "[PTZController] 开始自动查询角度，间隔:" << intervalMs << "ms";
}

void PTZController::stopAutoQuery()
{
    if (!m_autoQuerying) {
        return;
    }
    
    m_autoQuerying = false;
    m_autoQueryTimer->stop();
    qDebug() << "[PTZController] 停止自动查询角度";
}

bool PTZController::isAutoQuerying() const
{
    return m_autoQuerying;
}

void PTZController::onAutoQueryTimer()
{
    if (m_serial && m_serial->isOpen()) {
        queryAngle();
    }
}
