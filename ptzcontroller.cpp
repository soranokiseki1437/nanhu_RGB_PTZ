// 修复P3#25: 提取波特率为常量（在头文件中定义）
#include "ptzcontroller.h"
#include <QDebug>
#include <QThread>
#include <QTimer>
#include <QMutex>
#include <cmath>

// 1. 构造函数：只保存配置，不打开串口
PTZController::PTZController(const QString &portName, int baudRate, uint8_t address, QObject *parent)
    : QObject(parent), m_address(address), m_portName(portName), m_baudRate(baudRate), m_serial(nullptr),
      m_currentPanSpeed(0.0f), m_currentTiltSpeed(0.0f),
      m_autoQueryTimer(new QTimer(this)), m_autoQuerying(false)
{
    connect(m_autoQueryTimer, &QTimer::timeout, this, &PTZController::onAutoQueryTimer);

    // 命令队列节拍器：10ms 逐条出队，避免同一时刻连续写串口
    m_cmdTimer = new QTimer(this);
    m_cmdTimer->setInterval(10);
    connect(m_cmdTimer, &QTimer::timeout, this, &PTZController::onCmdTimerTimeout);

    // 连续运动失联保护：single-shot，每条连续运动命令重置（绝对定位不适用，见 moveTo）
    m_stopWatchdog = new QTimer(this);
    m_stopWatchdog->setSingleShot(true);
    m_stopWatchdog->setInterval(500);
    connect(m_stopWatchdog, &QTimer::timeout, this, &PTZController::onStopWatchdogTimeout);

    // 串口打开失败后的自动重连
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setInterval(2000);
    connect(m_reconnectTimer, &QTimer::timeout, this, &PTZController::onReconnectTimeout);
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

    openSerialPort();
}

// 打开串口：init 与自动重连定时器共用；失败时上报并启动重连
void PTZController::openSerialPort()
{
    if (!m_serial || m_serial->isOpen()) {
        return;
    }

    if (m_serial->open(QIODevice::ReadWrite)) {
        m_reconnectTimer->stop();
        m_noPortWarned = false;
        m_buffer.clear();
        qDebug() << "[成功] 串口已成功打开 ->" << m_portName;
    } else {
        qCritical() << "[错误] 串口打开失败:" << m_serial->errorString();
        emit serialError(QString("串口打开失败: %1").arg(m_serial->errorString()));
        if (!m_reconnectTimer->isActive()) {
            m_reconnectTimer->start();  // 2s 后自动重连
        }
    }
}

void PTZController::onReconnectTimeout()
{
    if (m_serial && m_serial->isOpen()) {
        m_reconnectTimer->stop();
        return;
    }
    qDebug() << "[PTZController] 尝试重新打开串口:" << m_portName;
    openSerialPort();
}

PTZController::~PTZController()
{
    stopAutoQuery();
    if (m_cmdTimer) m_cmdTimer->stop();
    if (m_stopWatchdog) m_stopWatchdog->stop();
    if (m_reconnectTimer) m_reconnectTimer->stop();
    if (m_serial && m_serial->isOpen()) {
        m_serial->close();
    }
}

void PTZController::prepareForDestruction()
{
    // 在 PTZ 线程中执行，确保串口在本线程关闭，避免跨线程析构
    stopAutoQuery();
    m_cmdTimer->stop();
    m_stopWatchdog->stop();
    m_reconnectTimer->stop();
    m_cmdQueue.clear();
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
        const qint64 written = m_serial->write(packet);
        if (written != packet.size()) {
            qWarning() << "[PTZ] 串口写入不完整:" << written << "/" << packet.size();
            emit serialError(QString("串口写入不完整(%1/%2)").arg(written).arg(packet.size()));
        }
        emit rawDataInout(true, packet); // 发送数据信号
        // qDebug() << "[发送数据]" << packet.toHex().toUpper();
    } else if (!m_noPortWarned) {
        // 串口未打开时只告警一次，避免跟踪闭环高频调用时刷日志
        m_noPortWarned = true;
        qWarning() << "[警告] 串口未打开，数据被丢弃！";
        emit serialError(QString("串口未打开，命令被丢弃"));
    }
}

// 绝对定位控制
void PTZController::moveTo(float pan, float tilt, float speed)
{
    // --- 安全限位：越界目标值直接夹紧，防止球机堵转 ---
    const float reqPan = pan;
    const float reqTilt = tilt;
    pan  = qBound(PAN_MIN,  pan,  PAN_MAX);
    tilt = qBound(TILT_MIN, tilt, TILT_MAX);
    if (!qFuzzyCompare(pan, reqPan) || !qFuzzyCompare(tilt, reqTilt)) {
        qWarning() << "[PTZ] 目标角度越界已夹紧 -> P:" << reqPan << "=>" << pan
                   << " T:" << reqTilt << "=>" << tilt;
    }

    // --- 第一步：设置定位速度 (指令 0x5F) ---
    uint8_t speedByte;
    if (speed <= 1.0f)
        speedByte = static_cast<uint8_t>(speed * 63);
    else
        speedByte = static_cast<uint8_t>(speed);

    if (speedByte > 0x3F) speedByte = 0x3F;

    QByteArray speedCmd = buildPelcoDPacket(0x00, 0x5F, speedByte, speedByte);

    // --- 第二步：水平角度定位 (指令 0x4B) ---
    int pa = static_cast<int>(pan * 100);
    QByteArray panCmd = buildPelcoDPacket(0x00, 0x4B,
                                          static_cast<uint8_t>(pa >> 8),
                                          static_cast<uint8_t>(pa & 0xFF));

    // --- 第三步：俯仰角度定位 (指令 0x4D) ---
    float protocolTilt = tilt;
    if (tilt < 0.0f) {
        protocolTilt = 360.0f + tilt;
    }

    int ta = static_cast<int>(protocolTilt * 100.0f);
    QByteArray tiltCmd = buildPelcoDPacket(0x00, 0x4D,
                                           static_cast<uint8_t>(ta >> 8),
                                           static_cast<uint8_t>(ta & 0xFF));

    // 新定位序列作废旧队列，由 m_cmdTimer 按 10ms 节拍依次发出。
    // （原实现用 QTimer::singleShot 延时 20/70ms 发送，stop() 无法取消，停止后仍会发出残留定位命令）
    m_cmdQueue.clear();
    m_cmdQueue.enqueue(speedCmd);
    m_cmdQueue.enqueue(panCmd);
    m_cmdQueue.enqueue(tiltCmd);
    m_cmdTimer->start();

    m_positioning = true;
    // 绝对定位会自行到位，不启用连续运动看门狗（否则 500ms 后会被误判失联而打断定位）
    m_stopWatchdog->stop();

    qDebug() << "[发送] 移动指令 -> P:" << pan << " T:" << tilt
             << " (协议映射值:" << protocolTilt << ") Spd:" << speedByte;
}

// 停止指令
void PTZController::stop()
{
    const bool pending = !m_cmdQueue.isEmpty() || m_cmdTimer->isActive();
    const bool moving  = (m_currentPanSpeed != 0.0f) || (m_currentTiltSpeed != 0.0f);

    // 已停稳且无在途命令：忽略重复 stop（跟踪闭环在目标居中时每帧都会调用，避免刷爆串口）
    if (!pending && !moving && !m_positioning) {
        return;
    }

    // 作废队列中残留的定位命令（moveTo 的多包序列不再发出），避免停止后仍有残留定位
    m_cmdQueue.clear();
    m_cmdTimer->stop();
    m_stopWatchdog->stop();

    // Pelco-D 无应答：立即连发 3 次防丢包。
    // 这里直接写串口缓冲区（不排队、不 msleep），既保证与后续命令的先后顺序，也不阻塞事件循环
    const QByteArray stopPkt = buildPelcoDPacket(0x00, 0x00, 0x00, 0x00);
    for (int i = 0; i < 3; ++i) {
        writeToSerial(stopPkt);
    }

    m_currentPanSpeed = 0.0f;
    m_currentTiltSpeed = 0.0f;
    m_positioning = false;
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

        // 异常值校验：合法角度范围 0~360.00°（16 位原始值最大 655.35）
        if (std::isnan(angle) || angle < 0.0 || angle > PAN_MAX) {
            qWarning() << "[PTZ] 收到异常角度，丢弃该包:" << angle;
            m_buffer.remove(0, 1);
            continue;
        }

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
    m_positioning = false;  // 连续运动与绝对定位互斥

    // 方向控制优先：作废在途的绝对定位序列，避免方向命令之后云台又跳到旧目标
    if (!m_cmdQueue.isEmpty()) {
        m_cmdQueue.clear();
        m_cmdTimer->stop();
    }

    // 组包：Cmd1固定0x00，Data1是水平速度，Data2是垂直速度
    QByteArray packet = buildPelcoDPacket(0x00, cmd2, hSpeed, vSpeed);

    // 直接发送到底层串口（单包命令，无需排队）
    writeToSerial(packet);

    if (cmd2 == 0x00) {
        m_stopWatchdog->stop();
    } else {
        // 重置失联保护：500ms 内没有新的连续运动命令时自动补发 stop
        m_stopWatchdog->start();
    }
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

// --- 命令队列出队发送（10ms 节拍） ---
void PTZController::onCmdTimerTimeout()
{
    if (m_cmdQueue.isEmpty()) {
        m_cmdTimer->stop();
        return;
    }
    writeToSerial(m_cmdQueue.dequeue());
}

// --- 连续运动失联保护：超时未收到新命令且速度非零 -> 补发 stop ---
void PTZController::onStopWatchdogTimeout()
{
    if (m_currentPanSpeed != 0.0f || m_currentTiltSpeed != 0.0f) {
        qWarning() << "[PTZ] 连续运动超时未收到新命令，自动补发停止指令";
        stop();
    }
}
