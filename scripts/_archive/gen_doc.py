from docx import Document
from docx.shared import Pt, Inches, Cm, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.oxml.ns import qn
from docx.oxml import OxmlElement

doc = Document()

sn = doc.styles['Normal']
sn.font.name = '微软雅黑'
sn.font.size = Pt(10.5)
sn.element.rPr.rFonts.set(qn('w:eastAsia'), '微软雅黑')

for s in doc.sections:
    s.top_margin = Cm(2); s.bottom_margin = Cm(2)
    s.left_margin = Cm(2); s.right_margin = Cm(2)

def H(text, level=1):
    h = doc.add_heading(text, level=level)
    for r in h.runs:
        r.font.name = '微软雅黑'; r.font.bold = True
        r.font.color.rgb = RGBColor(0x1F, 0x38, 0x60)
        if level == 1: r.font.size = Pt(18)
        elif level == 2: r.font.size = Pt(14)
        else: r.font.size = Pt(12)
    return h

def TBL(headers, rows):
    t = doc.add_table(rows=1, cols=len(headers))
    t.style = 'Table Grid'
    for i, h in enumerate(headers):
        c = t.rows[0].cells[i]
        c.text = ''
        r = c.paragraphs[0].add_run(h)
        r.bold = True; r.font.size = Pt(10.5); r.font.name = '微软雅黑'
        r.font.color.rgb = RGBColor(255, 255, 255)
        sh = OxmlElement('w:shd')
        sh.set(qn('w:val'), 'clear'); sh.set(qn('w:fill'), '1F3860')
        c._tc.get_or_add_tcPr().append(sh)
    for row in rows:
        tr = t.add_row().cells
        for i, val in enumerate(row):
            tr[i].text = ''
            r = tr[i].paragraphs[0].add_run(str(val))
            r.font.size = Pt(10); r.font.name = '微软雅黑'
    return t

def P(text=''):
    p = doc.add_paragraph()
    r = p.add_run(text)
    r.font.name = '微软雅黑'; r.font.size = Pt(10.5)
    return p

def CODE(text):
    p = doc.add_paragraph()
    r = p.add_run(text)
    r.font.name = 'Consolas'; r.font.size = Pt(9)
    p.paragraph_format.left_indent = Cm(0.8)
    sh = OxmlElement('w:shd')
    sh.set(qn('w:val'), 'clear'); sh.set(qn('w:fill'), 'F2F2F2')
    p._element.get_or_add_pPr().append(sh)
    return p

def DIAG(lines):
    p = doc.add_paragraph()
    for i, line in enumerate(lines):
        if i > 0: p.add_run().add_break()
        r = p.add_run(line); r.font.name = 'Consolas'; r.font.size = Pt(9)
    p.paragraph_format.left_indent = Cm(0.5)
    sh = OxmlElement('w:shd')
    sh.set(qn('w:val'), 'clear'); sh.set(qn('w:fill'), 'EBF4FA')
    p._element.get_or_add_pPr().append(sh)
    bd = OxmlElement('w:pBdr')
    l = OxmlElement('w:left')
    l.set(qn('w:val'), 'single'); l.set(qn('w:sz'), '12'); l.set(qn('w:color'), '4A90E2')
    bd.append(l)
    p._element.get_or_add_pPr().append(bd)
    return p

# ===== 标题页 =====
doc.add_paragraph()
t = doc.add_paragraph(); t.alignment = WD_ALIGN_PARAGRAPH.CENTER
r = t.add_run('RGB_PTZ_Integrated'); r.bold = True; r.font.size = Pt(28)
r.font.color.rgb = RGBColor(0x1F, 0x38, 0x60)
s = doc.add_paragraph(); s.alignment = WD_ALIGN_PARAGRAPH.CENTER
r = s.add_run('可见光相机 + PTZ 云台上位机\n目标跟踪系统技术说明文档')
r.font.size = Pt(14); r.font.color.rgb = RGBColor(0x4A, 0x90, 0xE2)
doc.add_paragraph(); doc.add_paragraph()
v = doc.add_paragraph(); v.alignment = WD_ALIGN_PARAGRAPH.CENTER
r = v.add_run('版本 v1.4  |  Qt 6.10 / C++17 / Windows')
r.font.size = Pt(11); r.font.color.rgb = RGBColor(0x88, 0x88, 0x88)
doc.add_page_break()

# ===== 目录 =====
H('目录', level=1)
for item in [
    '一、项目概述', '二、模块一览与文件清单',
    '三、系统架构与数据流', '四、线程模型与并发安全',
    '五、目标跟踪算法（DSST + KF）', '六、云台伺服控制算法（前馈 + PID）',
    '七、PTZ 控制协议（Pelco-D）', '八、UI 控件一览与使用说明',
    '九、参数整定与调试建议', '十、常见问题排查', '十一、变更记录']:
    p = doc.add_paragraph(); r = p.add_run(item); r.font.size = Pt(11)
doc.add_page_break()

# ===== 一、项目概述 =====
H('一、项目概述', level=1)
P('本项目是基于 Qt 开发的桌面上位机软件，用于驱动一台可见光相机 + 三轴 PTZ 云台。核心能力是：'
  '用户在画面中框选一个目标后，算法在后续每帧自动识别目标位移并驱动云台将目标保持在画面中央。')
P('工作流程：① 框选目标 → ② DSST 相关滤波检测目标位置 → ③ KF 平滑并预测 → ④ 前馈+PID 计算云台速度 '
  '→ ⑤ Pelco-D 串口发送 → ⑥ 云台跟随目标。')

H('技术栈', level=2)
TBL(['组件', '版本/说明'], [
    ['开发框架', 'Qt 6.10（MinGW 64-bit）'],
    ['语言', 'C++17'],
    ['视频解码', 'Univision SDK（YUV→QImage）'],
    ['录像', 'FFmpeg 8.1（avcodec/avformat/swscale）'],
    ['云台协议', 'Pelco-D（默认 9600 bps）'],
    ['跟踪算法', 'DSST 相关滤波 + 线性 KF + FHOG 33 尺度'],
    ['伺服控制', 'KF 预测前馈 + PID 反馈'],
    ['构建系统', 'qmake'],
])

# ===== 二、模块与文件 =====
H('二、模块一览与文件清单', level=1)
H('源代码文件', level=2)
TBL(['文件', '职责', '运行线程'], [
    ['main.cpp', '应用入口，注册 TrackResult 元类型', '主线程'],
    ['mainwindow.*', '主窗口、UI、状态管理', '主线程'],
    ['objecttracker.*', 'DSST + KF + FHOG 尺度估计', 'worker thread'],
    ['trackingcontroller.*', '伺服控制，脱靶量→速度指令', '主线程'],
    ['ptzcontroller.*', 'Pelco-D 协议与串口收发', 'PTZ 线程'],
    ['fftutils.*', '二维 FFT、Hanning 窗、复数工具', '由 tracker 调用'],
    ['devicemanager.*', '相机登录/断开/帧分发', '内部 DecodeThread'],
    ['videodecoder.*', '解码封装', 'DecodeThread'],
    ['DecodeThread.*', 'SDK 解码循环线程', 'DecodeThread'],
    ['RecordThread.*', 'FFmpeg 录像写入', 'RecordThread'],
    ['capturemanager.*', '抓图保存', '主线程'],
    ['lensmanager.*', '镜头变焦/聚焦', '主线程'],
    ['imageprocessor.*', '图像辅助', '按需'],
    ['configmanager.*', '配置文件读写', '主线程'],
    ['datatypes.h', 'TrackResult/公共结构体', '头文件'],
    ['ThreadSafeQueue.h', '模板线程安全队列', '头文件'],
])
H('配置/工程文件', level=2)
TBL(['文件', '说明'], [
    ['RGB_PTZ_Integrated.pro', 'qmake 工程（SOURCES/HEADERS/FORMS/LIBS）'],
    ['README.md', 'AI/维护者参考：架构、线程、修改约束'],
    ['项目说明/项目说明.docx', '本文档：人类开发者完整指南'],
    ['sdk/*', '第三方相机 SDK（头/库/DLL）'],
    ['ffmpeg-8.1-full_build-shared/*', 'FFmpeg 运行时'],
])

# ===== 三、架构 =====
H('三、系统架构与数据流', level=1)
H('分层架构', level=2)
DIAG([
    '┌──────────────────────────────────────────────────────┐',
    '│ 表示层（UI）                                         │',
    '│ MainWindow + .ui（按钮/SpinBox/状态/视频显示/跟踪框）│',
    '└──────────────┬──────────────────┬────────────────────┘',
    '               │ 信号              │ 绘制',
    '               ▼                   ▼',
    '┌──────────────────────┐ ┌──────────────────────────┐',
    '│ 业务逻辑层           │ │ TrackingController       │',
    '│ DeviceManager        │ │ 状态机 / 脱靶量 / PID    │',
    '│ CaptureManager       │ │                          │',
    '│ LensManager          │ │                          │',
    '└──────────┬───────────┘ └──────────┬───────────────┘',
    '           │                        │',
    '           ▼                        ▼',
    '┌──────────────────────┐ ┌──────────────────────────┐',
    '│ 数据层               │ │ ObjectTracker（worker）   │',
    '│ ConfigManager        │ │ DSST/KF/FHOG/FFT        │',
    '│ DataRecorder         │ │                          │',
    '└──────────────────────┘ └──────────┬───────────────┘',
    '                                     │',
    '                                     ▼',
    '                        ┌──────────────────────────┐',
    '                        │ 基础设施层               │',
    '                        │ fftutils / PTZController │',
    '                        │ QSerialPort / QThread    │',
    '                        └──────────────────────────┘',
])

H('核心数据流水线（从相机到云台）', level=2)
DIAG([
    '[1] 相机 SDK 产生 YUV 原始帧',
    '       │  DecodeThread 内部',
    '       ▼',
    '[2] YUV→RGB 转 QImage（imageprocessor）',
    '       │  emit newFrame(frame)',
    '       ▼',
    '[3] MainWindow::onNewFrame 接收',
    '       ├─▶ 视频显示（paintEvent/QLabel）',
    '       └─▶ TrackingController::processFrame(frame)',
    '             │ QueuedConnection 跨线程投递',
    '             ▼',
    '[4] ObjectTracker::update(frame)    ← worker thread',
    '       ├─ 以 KF 预测位置为中心提取样本',
    '       ├─ FFT2D + 相关滤波 → 响应图',
    '       ├─ findMaxResponse → detX, detY',
    '       ├─ FHOG 33 尺度估计 → scale',
    '       ├─ KF 预测 → 检测 → KF 更新（对称协方差）',
    '       ├─ PSR 三态机（Normal/LowPSR/Occlusion）',
    '       ├─ 遮挡时 9 点重检测 + 备份滤波器混合',
    '       ▼',
    '[5] TrackResult{kfX,kfY,kfVx,kfVy,bbox,psr,...}',
    '       │ emit trackingDone(result) 回到主线程',
    '       ▼',
    '[6] TrackingController::computePTZControl(result)',
    '       ├─ predictX = kfX + kfVx * T_predict',
    '       ├─ error = predict - 画面中心',
    '       ├─ 死区判断、vFeed = K_ff*kfV',
    '       ├─ PID = P + clamp(I,±Imax) + D',
    '       ├─ speed = vFeed + PID → clamp(±maxSpeed)',
    '       ▼',
    '[7] emit ptzControlDelta(deltaX,deltaY,speedX,speedY)',
    '       ▼',
    '[8] MainWindow::onPtzControlDelta → cmd2 方向位组合',
    '       ▼',
    '[9] PTZController::moveDirection(cmd2,hSpeed,vSpeed)',
    '       ├─ 构建 Pelco-D 7 字节帧 + 校验和',
    '       └─ QSerialPort::write 发送到云台',
])

# ===== 四、线程模型 =====
H('四、线程模型与并发安全', level=1)
P('本项目有 8 个活跃线程。修改代码前务必确认当前代码在哪个线程执行。')

H('线程清单', level=2)
TBL(['线程', '入口', '生命周期', '可访问资源'], [
    ['主线程', 'main()', '应用全程', '所有 UI 控件、TrackingController、PTZController 公共接口'],
    ['DecodeThread', 'DeviceManager::start()', '相机登录→退出', 'SDK 句柄、YUV 缓冲'],
    ['RecordThread', 'startRecord()', '录像期间', 'FFmpeg 输出文件、编码缓冲'],
    ['ObjectTracker worker', 'setTarget() 创建', '框选→停止', 'ObjectTracker 私有成员、KF、滤波器'],
    ['PTZ worker', 'PTZController 内部', '串口打开期间', '串口句柄、Pelco-D 数据'],
    ['QSerialPort 内部', 'Qt 内部', '串口打开期间', 'Qt 管理、业务代码勿直访问'],
    ['Qt 事件派发', 'Qt 内部', '全程', 'UI 事件循环'],
    ['QtConcurrent 池', '按需', '短时任务', '图像缩放/转换'],
])

H('ObjectTracker 线程安全约定', level=2)
P('① init() 在主线程同步调用（分配内存、初始化滤波器、写入首帧）。')
P('② processFrameSlot() 通过 QueuedConnection 在 worker 线程执行，是唯一 heavy compute 入口。')
P('③ 销毁顺序必须严格：disconnect → quit() → wait() → delete tracker → delete thread。'
  '见 trackingcontroller.cpp 的 setTarget() 和 stopTracking()。**不要改动此顺序**，否则极易跨线程删除崩溃。')

# ===== 五、跟踪算法 =====
H('五、目标跟踪算法（DSST + 卡尔曼滤波）', level=1)
H('5.1 DSST 相关滤波（平移）', level=2)
P('首帧在目标周围裁剪一个加 Hanning 窗的样本，FFT 到频域，与标签的频域表示构造"相关滤波器分子/分母"。'
  '后续每帧在候选位置抽取样本，与滤波器做频域点乘 → iFFT → 2D 响应图。峰值位置即目标新位置。')
CODE(
    '// objecttracker.cpp 核心三步：\n'
    '1) sample = getTranslationSample(image, kfPredX, kfPredY, currentScale);\n'
    '2) response = computeResponse(sample);\n'
    '3) findMaxResponse(response, maxRow, maxCol);  // 峰值 = 目标新位置'
)

H('5.2 尺度估计（FHOG + 33 尺度）', level=2)
P('在目标位置附近抽取 33 个不同尺度的图像块，对每块提取 FHOG 特征（31 通道直方图方向梯度），'
  '与预训练的尺度滤波器做相关。最大响应对应的尺度因子 × 当前尺度 = 新目标大小。')

H('5.3 卡尔曼滤波（位置+速度）', level=2)
P('状态向量 x = [px, py, vx, vy]^T。')
DIAG([
    '预测步（每帧开头）：',
    '  x_pred = A * x,   A = [[1,0,1,0],[0,1,0,1],[0,0,1,0],[0,0,0,1]]',
    '  P_pred = A * P * A^T + Q    （遮挡时 Q 放大，允许更大不确定性）',
    '',
    '更新步（检测成功后）：',
    '  H = [[1,0,0,0],[0,1,0,0]]    （位置可观测，速度不可直接观测）',
    '  S = H * P * H^T + R',
    '  K = P * H^T * S^{-1}',
    '  x = x_pred + K * (z - H * x_pred)',
    '  P = (I - K*H) * P         并执行 P = (P+P^T)/2 强制对称',
    '',
    '速度融合：x[2..3] = 0.6*KF + 0.4*(meas - prevPos)，阻尼抑制测量抖动',
])

H('5.4 遮挡检测与三态机', level=2)
DIAG([
    'Normal ── PSR < 0.75 × baseline 连续 3 帧 ──▶ Occlusion',
    '  ▲                                                    │',
    '  │ 9 点重检测成功 + 备份滤波器混合(0.6备份+0.4当前)   │ 纯 KF 预测位置',
    '  └────────────────────────────────────────────────────┘',
    '',
    'PSR = (peak - 周围均值) / 周围标准差   （响应图的"峰旁比"）',
])

# ===== 六、伺服算法 =====
H('六、云台伺服控制算法（前馈 + PID）', level=1)
H('6.1 为什么不是"纯 P"', level=2)
TBL(['问题', '原因'], [
    ['稳态误差', '目标匀速运动时永远追不上；机械死区在小脱靶量时不响应'],
    ['振荡/过冲', '目标靠近中心时速度仍大，云台冲到对面才反向，画面晃动'],
    ['延迟补偿不足', '云台收到指令→加速→到位≈80 ms，而帧间隔仅 40 ms'],
])

H('6.2 改进方案框图', level=2)
DIAG([
    '   ┌─────────────── 卡尔曼滤波 ────────────────┐',
    '   │   输出：kfX, kfY, kfVx, kfVy                │',
    '   └────────────┬─────────────────┬─────────────┘',
    '                │ 位置             │ 速度',
    '                ▼                  ▼',
    '   ┌─────────────────┐   ┌──────────────────┐',
    '   │ 预测 T_predict  │   │ 速度前馈          │',
    '   │ predict=pos+v*T │   │ vFeed = K_ff*v    │',
    '   └───────┬─────────┘   └────────┬─────────┘',
    '           │                      │',
    '  error = predict - 画面中心       │',
    '           │                      │',
    '           ▼                      ▼',
    '   ┌──────────────── PID ────────────────┐',
    '   │ P = Kp*error                       │',
    '   │ I = clamp(I + Ki*error, ±Imax)      │',
    '   │ D = Kd*(error-error_prev)           │',
    '   │ out_PID = P + I + D                 │',
    '   └──────────────┬──────────────────────┘',
    '                  │',
    '         speed = vFeed + out_PID',
    '                  │',
    '   ┌──────────────┴─────────────────┐',
    '   │ |error|<deadZone → speed=0     │',
    '   │ clamp(-maxSpeed, +maxSpeed)    │',
    '   └──────────────┬─────────────────┘',
    '                  ▼',
    '     Pelco-D 速度指令（方向位+速度字节）',
])

H('6.3 默认参数表', level=2)
TBL(['参数', '符号', '默认值', '含义'], [
    ['比例增益', 'Kp', '0.50', '误差越大→速度越大'],
    ['积分增益', 'Ki', '0.02', '消除稳态偏差（安装倾斜/重力）'],
    ['微分增益', 'Kd', '0.30', '抑制振荡，目标靠近中心时减速'],
    ['速度前馈', 'K_ff', '0.15', '把 KF 估计的目标速度直接映射为云台速度'],
    ['预测时域', 'T_predict', '2 帧', '向前预测 2 帧位置（≈80 ms 机械延迟）'],
    ['死区', 'deadZone', '20 px', '小于此值不产生动作，防止抖动'],
    ['最大速度', 'maxSpeed', '20', 'Pelco-D 速度字节上限 [0,63]'],
    ['积分上限', 'I_max', '15', '防止积分饱和（目标跑出后残留大积分导致反向冲）'],
])

H('6.4 参数整定步骤', level=2)
P('① 先令 Ki=Kd=0、K_ff=0，只调 Kp。从小到大调到目标轻微振荡再回调 20%。')
P('② 逐渐加大 Kd，直到振荡消失且目标能被"吸"回中心。')
P('③ 加大 K_ff，直到匀速跟踪时稳态脱靶量接近 0。')
P('④ 最后加 Ki（很小），消除残留静态偏差。Ki 过大会产生低频振荡。')
P('⑤ T_predict 与实际云台延迟有关。目标从左向右移动时云台若"总是跟在后面"，可把 T_predict 调到 3~4；'
  '若目标到了中心云台还继续转（过冲），T_predict 太大，调回 1~2。')

# ===== 七、Pelco-D =====
H('七、PTZ 控制协议（Pelco-D）', level=1)
H('7.1 帧格式（7 字节）', level=2)
DIAG([
    ' Byte0   Byte1   Byte2   Byte3   Byte4   Byte5   Byte6',
    '┌──────┬──────┬───────┬───────┬───────┬───────┬───────┐',
    '│ 0xFF │ Addr │ Cmd1  │ Cmd2  │ Data1 │ Data2 │ Check │',
    '└──────┴──────┴───────┴───────┴───────┴───────┴───────┘',
    '',
    ' Check = (Addr + Cmd1 + Cmd2 + Data1 + Data2) & 0xFF   （低 8 位和）',
])

H('7.2 常用 Cmd2 位定义', level=2)
TBL(['位', '值', '含义'], [
    ['bit 0', '0x01', 'Focus Close（聚焦+）'],
    ['bit 1', '0x02', 'Pan Right（云台右转）'],
    ['bit 2', '0x04', 'Pan Left（云台左转）'],
    ['bit 3', '0x08', 'Tilt Up（云台向上）'],
    ['bit 4', '0x10', 'Tilt Down（云台向下）'],
    ['bit 5', '0x20', 'Zoom Tele（变焦+）'],
    ['bit 6', '0x40', 'Zoom Wide（变焦-）'],
])
P('多个位可同时置 1，实现同时平摇+俯仰。')

H('7.3 速度字段', level=2)
P('Data1 = Pan 速度（0x00~0x3F = 63 档）；Data2 = Tilt 速度（0x00~0x3F）。值为 0 表示该轴停止。')

H('7.4 方向约定（负反馈闭环）', level=2)
DIAG([
    ' 目标在画面中心 X 方向位置 kfX，画面宽 W → 中心 W/2：',
    '',
    '   kfX > W/2 → 目标在右侧 → Pan Right（cmd2 |= 0x02）',
    '   kfX < W/2 → 目标在左侧 → Pan Left （cmd2 |= 0x04）',
    '   kfY > H/2 → 目标在下方 → Tilt Down （cmd2 |= 0x10）',
    '   kfY < H/2 → 目标在上方 → Tilt Up   （cmd2 |= 0x08）',
    '',
    ' 脱靶量为 0 → cmd2=0 → 云台停止 → 目标稳定在画面中心',
])

# ===== 八、UI =====
H('八、UI 控件一览与使用说明', level=1)
P('所有跟踪相关控件放在"跟踪控制"分组框（QGroupBox）内。')

H('8.1 控制按钮', level=2)
TBL(['控件 objectName', '类型', '功能'], [
    ['btnTrackSelect', 'QPushButton', '进入"框选模式"——在视频画面中用鼠标拖矩形选目标'],
    ['btnTrackStart', 'QPushButton', '读取所有参数并启动跟踪（创建 ObjectTracker worker thread）'],
    ['btnTrackStop', 'QPushButton', '停止跟踪、销毁 worker、云台停止转动'],
    ['labelTrackStatus', 'QLabel（BoxFrame）', '显示当前状态：空闲/等待框选/跟踪中/丢失...'],
])

H('8.2 跟踪参数（7 个 QSpinBox）', level=2)
TBL(['控件 objectName', '默认值', '范围', '对应算法参数'], [
    ['spinTrackDeadZone', '20', '5~200', '死区（像素）'],
    ['spinTrackMaxSpeed', '20', '1~63', '最大速度（Pelco-D Data）'],
    ['spinTrackKp', '50', '0~200', '比例增益×100（实际 0.50）'],
    ['spinTrackKi', '2', '0~100', '积分增益×100（实际 0.02）'],
    ['spinTrackKd', '30', '0~200', '微分增益×100（实际 0.30）'],
    ['spinTrackKff', '15', '0~100', '速度前馈增益×100（实际 0.15）'],
    ['spinTrackPredict', '2', '0~10', '预测时域（帧）'],
])
P('注意：所有 K* 参数在 UI 上均放大 100 倍。例如 spinTrackKp=50 → 代码除以 100 得 Kp=0.50。')

H('8.3 典型操作流程', level=2)
DIAG([
    '步骤 1：点击 [框选目标] → 鼠标在视频上拖一个矩形',
    '步骤 2：调整 7 个跟踪参数（或保持默认）',
    '步骤 3：点击 [开始跟踪] → 状态标签变"跟踪中"，云台开始转动',
    '步骤 4：观察目标是否稳定在画面中心，必要时调整 Kp/Kd/Kff/T_predict',
    '步骤 5：点击 [停止跟踪] 结束',
])

# ===== 九、调试建议 =====
H('九、参数整定与调试建议', level=1)
H('9.1 调试信息', level=2)
TBL(['信息', '含义', '排查用途'], [
    ['状态切换日志', 'Idle/Selecting/Tracking', '确认状态机流转'],
    ['kfX/Y/Vx/Vy', 'KF 平滑后的位置/速度', '判断是否平滑、是否有异常漂移'],
    ['PSR', '响应图峰旁比', '判断跟踪质量（<0.75 baseline 视为质量下降）'],
    ['occluded', '是否在遮挡', '验证遮挡检测是否漏判/误判'],
    ['delta/speed', '最终脱靶量与云台速度', '检查死区、限幅是否生效'],
    ['Pelco-D 发送错误', '串口写失败', '检查波特率、串口线、云台供电'],
])

H('9.2 常见异常现象', level=2)
TBL(['现象', '可能原因', '建议'], [
    ['目标中心左右振荡', 'Kp 过大 / Kd 太小', '降低 Kp，或加大 Kd'],
    ['目标移动时云台不动', '死区过大、Kp 过小、T_predict=0', '减小 deadZone，增大 Kp，设 T_predict=2'],
    ['云台追过目标（过冲）', 'T_predict 过大 / Kff 过大', '减小 T_predict，减小 Kff'],
    ['匀速目标云台总是慢半拍', 'Kff 太小', '加大 Kff 到 0.20~0.30'],
    ['目标偏着"一侧"跟踪', '积分太小、或存在机械偏差', '加大 Ki 或物理校正云台安装'],
    ['遮挡恢复失败', '备份滤波器漂移、搜索半径不够', '检查 reDetect 逻辑与 baseline 锁定'],
    ['启动跟踪瞬间云台大跳', 'PID 积分非零、上一帧误差未清', '确认 setTarget 会调 resetPidState()'],
])

H('9.3 性能与实时性', level=2)
P('跟踪一帧的主要计算量来自 FFT（平移+尺度两个通道）。在 720p + i5-12 代 CPU 上实测单帧约 8~15 ms，25 fps 完全不阻塞 UI。')
P('若需进一步加速：启用 OpenMP 并行化 FHOG；缩小尺度数量；降低 HOG 方向数。')

# ===== 十、FAQ =====
H('十、常见问题排查', level=1)
TBL(['问题', '定位步骤'], [
    ['点击开始跟踪后云台无反应', '① 确认串口已连接；② 查看 ptzControlDelta 是否被发射；③ 用串口助手观察 7 字节帧'],
    ['跟踪框不随目标移动', '① 检查 ObjectTracker worker 线程是否启动；② PSR 是否持续低；③ 目标是否被遮挡'],
    ['运行几分钟后崩溃', '检查是否跨线程 delete tracker（应走 disconnect→quit→wait→delete 顺序）；检查内存泄漏'],
    ['重启程序后目标位置偏差', '确认每次 setTarget 会调用 resetPidState()，否则积分会残留'],
    ['新增 UI 控件编译报 member not found', '运行 qmake 重新生成 ui_mainwindow.h（或删除 build 目录）'],
    ['KF 速度估计越来越偏', '检查 Q/R 矩阵是否合适；是否 PSR 低时仍在 KF update（应在遮挡时只 predict）'],
])

# ===== 十一、变更记录 =====
H('十一、变更记录', level=1)
P('• v1.4 — 伺服升级为"KF 预测前馈 + PID"；KF 采样中心从旧位置改为 KF 预测位置；新增 Ki/Kd/Kff/预测时域 4 个 UI 控件；PTZ 速度从截断改为四舍五入；同步更新 README.md 与本文档。')
P('• v1.3 — 修复 KF 协方差预测公式与尺度滤波器分母；统一跨线程销毁顺序；扩展 TrackResult 字段。')
P('• v1.2 — 引入独立 worker thread，ObjectTracker 计算不再阻塞 UI。')
P('• v1.1 — 新增 Pelco-D 协议自动校验和与组合方向指令。')
P('• v1.0 — 首个可用版本：相机预览、PTZ 手动控制、DSST 单目标跟踪。')

out_path = r'D:\lx\RGB_PTZ\RGB_PTZ_Integrated\项目说明\项目说明.docx'
doc.save(out_path)
print(f'OK -> {out_path}')
