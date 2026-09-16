const {
  Document, Packer, Paragraph, TextRun, Table, TableRow, TableCell,
  AlignmentType, LevelFormat, HeadingLevel, BorderStyle, WidthType,
  ShadingType, PageBreak, PageNumber, Header, Footer, SectionType,
  UnderlineType, TabStopType, TabStopPosition
} = require('docx');
const fs = require('fs');

// ========== Utility Functions ==========

function makeRun(text, opts = {}) {
  return new TextRun({
    text,
    font: opts.font || "Microsoft YaHei",
    size: opts.size || 24,
    bold: opts.bold || false,
    italics: opts.italics || false,
    color: opts.color || "000000",
    underline: opts.underline || undefined,
    highlight: opts.highlight || undefined,
    strike: opts.strike || undefined,
    allCaps: opts.allCaps || false,
    smallCaps: opts.smallCaps || false,
  });
}

function makePara(children, opts = {}) {
  return new Paragraph({
    children,
    alignment: opts.alignment || AlignmentType.LEFT,
    heading: opts.heading || undefined,
    spacing: opts.spacing || { after: 120, before: opts.heading ? 200 : 0 },
    indent: opts.indent || undefined,
    border: opts.border || undefined,
    pageBreakBefore: opts.pageBreakBefore || false,
    keepNext: opts.keepNext || false,
  });
}

function makeCell(text, opts = {}) {
  const borderObj = {
    top: { style: BorderStyle.SINGLE, size: 1, color: "CCCCCC" },
    bottom: { style: BorderStyle.SINGLE, size: 1, color: "CCCCCC" },
    left: { style: BorderStyle.SINGLE, size: 1, color: "CCCCCC" },
    right: { style: BorderStyle.SINGLE, size: 1, color: "CCCCCC" },
  };
  const paraChildren = typeof text === 'string' ? [makeRun(text, { size: opts.size || 20 })] : text;

  return new TableCell({
    borders: borderObj,
    width: opts.width || undefined,
    shading: opts.shading ? { fill: opts.shading, type: ShadingType.CLEAR } : undefined,
    margins: { top: 40, bottom: 40, left: 80, right: 80 },
    children: [makePara(paraChildren, { alignment: opts.alignment || AlignmentType.LEFT })],
    verticalAlign: opts.verticalAlign || undefined,
  });
}

function makeHeaderCell(text, opts = {}) {
  return makeCell(text, {
    ...opts,
    shading: "4472C4",
    bold: true,
    alignment: opts.alignment || AlignmentType.CENTER,
  });
}

function makeTable(headers, rows, colWidths) {
  const tableWidth = colWidths ? colWidths.reduce((a, b) => a + b, 0) : 9360;
  return new Table({
    width: { size: tableWidth, type: WidthType.DXA },
    columnWidths: colWidths || headers.map(() => Math.floor(9360 / headers.length)),
    rows: [
      new TableRow({
        children: headers.map(h => makeHeaderCell(h)),
      }),
      ...rows.map(row => new TableRow({
        children: row.map(cell => makeCell(cell)),
      })),
    ],
  });
}

function makeBulletPoint(text, level = 0) {
  return new Paragraph({
    children: [makeRun(text, { size: 22 })],
    numbering: { reference: "bullets", level },
    spacing: { after: 80 },
  });
}

function makeNumberedPoint(text, level = 0) {
  return new Paragraph({
    children: [makeRun(text, { size: 22 })],
    numbering: { reference: "numbers", level },
    spacing: { after: 80 },
  });
}

function makeCodeBlock(text) {
  const lines = text.trim().split('\n');
  const codeBorder = {
    top: { style: BorderStyle.SINGLE, size: 4, color: "E0E0E0" },
    bottom: { style: BorderStyle.SINGLE, size: 4, color: "E0E0E0" },
    left: { style: BorderStyle.SINGLE, size: 4, color: "E0E0E0" },
    right: { style: BorderStyle.SINGLE, size: 4, color: "E0E0E0" },
  };
  return new Paragraph({
    children: lines.map((line, i) => makeRun(line, {
      font: "Consolas",
      size: 18,
      color: "333333",
    })),
    spacing: { before: 100, after: 100, line: 300 },
    border: codeBorder,
    shading: { fill: "F5F5F5", type: ShadingType.CLEAR },
    indent: { left: 360, right: 360 },
  });
}

function makeAsciiDiagram(text) {
  const lines = text.trim().split('\n');
  const diagBorder = {
    top: { style: BorderStyle.SINGLE, size: 2, color: "2E75B6" },
    bottom: { style: BorderStyle.SINGLE, size: 2, color: "2E75B6" },
    left: { style: BorderStyle.SINGLE, size: 2, color: "2E75B6" },
    right: { style: BorderStyle.SINGLE, size: 2, color: "2E75B6" },
  };
  return new Paragraph({
    children: lines.map(line => makeRun(line, {
      font: "Consolas",
      size: 16,
      color: "1A1A2E",
    })),
    spacing: { before: 120, after: 120, line: 280 },
    border: diagBorder,
    shading: { fill: "F0F7FF", type: ShadingType.CLEAR },
    indent: { left: 240, right: 240 },
  });
}

// ========== Main Document Generation ==========

const h1Style = { size: 32, bold: true, font: "Microsoft YaHei", color: "1F3864" };
const h2Style = { size: 28, bold: true, font: "Microsoft YaHei", color: "2E5090" };
const h3Style = { size: 26, bold: true, font: "Microsoft YaHei", color: "3E72B0" };
const h4Style = { size: 24, bold: true, font: "Microsoft YaHei", color: "4A82C4" };
const bodyStyle = { size: 22, font: "Microsoft YaHei" };

const doc = new Document({
  numbering: {
    config: [
      {
        reference: "bullets",
        levels: [{
          level: 0,
          format: LevelFormat.BULLET,
          text: "\u2022",
          alignment: AlignmentType.LEFT,
          style: { paragraph: { indent: { left: 720, hanging: 360 } } }
        }]
      },
      {
        reference: "numbers",
        levels: [{
          level: 0,
          format: LevelFormat.DECIMAL,
          text: "%1.",
          alignment: AlignmentType.LEFT,
          style: { paragraph: { indent: { left: 720, hanging: 360 } } }
        }]
      },
    ]
  },
  styles: {
    default: { document: { run: { font: "Microsoft YaHei", size: 22 } } },
    paragraphStyles: [
      {
        id: "Heading1", name: "Heading 1", basedOn: "Normal", next: "Normal", quickFormat: true,
        run: { size: 32, bold: true, font: "Microsoft YaHei", color: "1F3864" },
        paragraph: { spacing: { before: 360, after: 240 }, outlineLevel: 0 }
      },
      {
        id: "Heading2", name: "Heading 2", basedOn: "Normal", next: "Normal", quickFormat: true,
        run: { size: 28, bold: true, font: "Microsoft YaHei", color: "2E5090" },
        paragraph: { spacing: { before: 280, after: 180 }, outlineLevel: 1 }
      },
      {
        id: "Heading3", name: "Heading 3", basedOn: "Normal", next: "Normal", quickFormat: true,
        run: { size: 26, bold: true, font: "Microsoft YaHei", color: "3E72B0" },
        paragraph: { spacing: { before: 200, after: 120 }, outlineLevel: 2 }
      },
      {
        id: "Heading4", name: "Heading 4", basedOn: "Normal", next: "Normal", quickFormat: true,
        run: { size: 24, bold: true, font: "Microsoft YaHei", color: "4A82C4" },
        paragraph: { spacing: { before: 160, after: 100 }, outlineLevel: 3 }
      },
    ]
  },
  sections: [{
    properties: {
      page: {
        size: { width: 11906, height: 16838 },
        margin: { top: 1440, right: 1080, bottom: 1440, left: 1080 }
      }
    },
    headers: {
      default: new Header({
        children: [new Paragraph({
          children: [
            makeRun("RGB_PTZ_Integrated 项目说明", { size: 18, color: "888888" }),
          ],
          alignment: AlignmentType.LEFT,
        })]
      })
    },
    footers: {
      default: new Footer({
        children: [new Paragraph({
          children: [
            makeRun("文档版本: 1.0.0  |  ", { size: 16, color: "888888" }),
            makeRun("页 ", { size: 16, color: "888888" }),
            makeRun({ children: [PageNumber.CURRENT], size: 16, color: "888888" }),
            makeRun(" / ", { size: 16, color: "888888" }),
            makeRun({ children: [PageNumber.TOTAL_PAGES], size: 16, color: "888888" }),
          ],
          alignment: AlignmentType.CENTER,
        })]
      })
    },
    children: [
      // ===== COVER PAGE =====
      makePara([makeRun("", { size: 40 })], { spacing: { before: 0 } }),
      makePara([makeRun("RGB_PTZ_Integrated", h1Style)], { alignment: AlignmentType.CENTER, spacing: { before: 400, after: 100 } }),
      makePara([makeRun("项目说明文档", { size: 36, bold: true, font: "Microsoft YaHei", color: "2E5090" })], { alignment: AlignmentType.CENTER, spacing: { after: 400 } }),
      makePara([], { spacing: { before: 200 } }),
      makePara([makeRun("RGB相机 + PTZ云台集成跟踪系统", { size: 26, font: "Microsoft YaHei", color: "666666" })], { alignment: AlignmentType.CENTER, spacing: { after: 100 } }),
      makePara([makeRun("基于DSST+卡尔曼滤波的目标视觉跟踪", { size: 26, font: "Microsoft YaHei", color: "666666" })], { alignment: AlignmentType.CENTER, spacing: { after: 100 } }),
      makePara([makeRun("通过Pelco-D串口与SDK协同控制云台实时锁定目标", { size: 24, font: "Microsoft YaHei", color: "666666" })], { alignment: AlignmentType.CENTER, spacing: { after: 600 } }),
      makePara([], { spacing: { before: 200 } }),

      // Info table
      new Table({
        width: { size: 6000, type: WidthType.DXA },
        columnWidths: [2000, 4000],
        borders: {
          top: { style: BorderStyle.NONE },
          bottom: { style: BorderStyle.NONE },
          left: { style: BorderStyle.NONE },
          right: { style: BorderStyle.NONE },
          insideHorizontal: { style: BorderStyle.NONE },
          insideVertical: { style: BorderStyle.NONE },
        },
        rows: [
          new TableRow({ children: [makeCell("项目版本", { bold: true, size: 22 }), makeCell("1.0.0", { size: 22 })] }),
          new TableRow({ children: [makeCell("GUI框架", { bold: true, size: 22 }), makeCell("Qt 6", { size: 22 })] }),
          new TableRow({ children: [makeCell("编程语言", { bold: true, size: 22 }), makeCell("C++17", { size: 22 })] }),
          new TableRow({ children: [makeCell("视频解码", { bold: true, size: 22 }), makeCell("FFmpeg 8.1", { size: 22 })] }),
          new TableRow({ children: [makeCell("运行平台", { bold: true, size: 22 }), makeCell("Windows (MinGW 64-bit)", { size: 22 })] }),
          new TableRow({ children: [makeCell("生成日期", { bold: true, size: 22 }), makeCell("2026年6月9日", { size: 22 })] }),
        ],
      }),

      new Paragraph({ children: [new PageBreak()] }),

      // ===== TABLE OF CONTENTS =====
      makePara([makeRun("目录", h1Style)], { spacing: { before: 200 } }),
      new Paragraph({
        children: [new TextRun("Table of Contents")],
        heading: HeadingLevel.HEADING_1,
        spacing: { after: 200 }
      }),
      new Paragraph({
        children: [new TextRun("此处目录由Word自动生成")],
        alignment: AlignmentType.CENTER,
        spacing: { after: 200, before: 100 }
      }),

      new Paragraph({
        children: [
          new TextRun("提示：打开文档后按 Ctrl+A 全选，然后按 F9 键可自动更新目录。"),
        ],
        alignment: AlignmentType.CENTER,
        spacing: { after: 200 }
      }),

      new Paragraph({ children: [new PageBreak()] }),

      // ===== 1. 项目概述 =====
      makePara([makeRun("1. 项目概述", h1Style)], { heading: HeadingLevel.HEADING_1 }),

      makePara([makeRun("1.1 项目简介", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makeTable(
        ["项目", "说明"],
        [
          ["项目名称", "RGB_PTZ_Integrated"],
          ["一句话描述", "一套将RGB网络相机视频流解码、基于DSST+Kalman Filter算法进行视觉目标跟踪，并通过Pelco-D串口协议与设备SDK协同控制PTZ云台实现目标自动锁定的桌面端集成系统"],
          ["版本号", "1.0.0"],
        ],
        [2000, 7360]
      ),

      makePara([makeRun("1.2 核心功能列表", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makeTable(
        ["序号", "功能模块", "详细说明"],
        [
          ["1", "PTZ云台控制", "通过RS-232串口使用Pelco-D协议控制云台水平/俯仰角度、速度、预置位；支持绝对定位、相对运动（方向盘式）、角度查询（10Hz自动查询）"],
          ["2", "RGB相机接入", "通过UNIVision SDK接入网络相机，支持设备登录/注销、主/子码流实时预览、H.264/H.265视频流解码、视频配置查询"],
          ["3", "目标跟踪（DSST+KF）", "自实现DSST（Discriminative Scale Space Tracking）+ 卡尔曼滤波跟踪算法；支持遮挡检测、自动恢复、三态状态机（正常/过渡/遮挡）"],
          ["4", "PTZ自动跟踪闭环", "跟踪器输出脱靶量 → P控制器计算PTZ速度 → 死区过滤 → 限幅 → Pelco-D方向指令 → 云台转动，形成闭环负反馈控制"],
          ["5", "镜头控制", "变倍(Zoom In/Out)、聚焦(Focus Near/Far)、光圈(Iris Open/Close)控制；支持自动/手动聚焦模式切换、一键聚焦"],
          ["6", "图像抓拍", "单次JPEG抓拍、定时抓拍（可配置间隔），通过SDK回调获取JPEG数据，后台线程处理保存"],
          ["7", "录像功能", "H.264编码MP4录像（转码方案：解码RGB → 编码H.264 → MP4封装），支持首帧获取真实分辨率自适应"],
          ["8", "数据记录", "以10Hz频率同步记录PTZ角度/速度与跟踪目标数据（目标坐标、脱靶量、置信度），录像结束时自动导出CSV"],
          ["9", "OSD控制", "通过SDK控制相机屏幕显示（OSD）：时间/星期显示开关、位置设置、字体大小（16×16 ~ 64×64四档）"],
          ["10", "配置管理", "设备连接参数（IP/用户名/密码）、抓拍参数（间隔/质量/保存路径）通过QSettings持久化到Windows注册表"],
        ],
        [800, 2000, 6560]
      ),

      makePara([makeRun("1.3 目标用户", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makeBulletPoint("安防监控系统集成商 — 需要PTZ自动跟踪功能的工程部署人员"),
      makeBulletPoint("PTZ云台设备测试工程师 — 需要测试云台控制精度、响应速度的硬件测试人员"),
      makeBulletPoint("视觉跟踪算法研究与开发人员 — 需要验证DSST+KF算法在实际场景中表现的研究人员"),

      makePara([makeRun("1.4 技术栈", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makeTable(
        ["类别", "技术选型", "备注"],
        [
          ["编程语言", "C++17", ""],
          ["GUI框架", "Qt 6 (Core, GUI, Widgets, SerialPort, Concurrent)", ""],
          ["视频解码", "FFmpeg 8.1 (libavcodec, libavformat, libswscale, libavutil)", ""],
          ["相机SDK", "UNIVision SDK (univisionsdk.dll)", "设备厂商提供"],
          ["云台协议", "Pelco-D (串口RS-232, 9600波特率, 8N1)", ""],
          ["跟踪算法", "DSST + Kalman Filter (自实现，参考MATLAB)", ""],
          ["数学运算", "FFT (Cooley-Tukey), FHOG特征提取 (自实现)", ""],
          ["构建系统", "qmake (Qt Creator)", ""],
          ["编译器", "MinGW 64-bit (GCC)", ""],
          ["运行平台", "Windows 10/11", ""],
        ],
        [2000, 5000, 2360]
      ),

      new Paragraph({ children: [new PageBreak()] }),

      // ===== 2. 完整功能介绍 =====
      makePara([makeRun("2. 完整功能介绍", h1Style)], { heading: HeadingLevel.HEADING_1 }),

      // 2.1 PTZ云台控制
      makePara([makeRun("2.1 PTZ云台控制", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makePara([makeRun("功能描述", { bold: true, size: 24 })], { spacing: { before: 100, after: 80 } }),
      makePara([makeRun("PTZController 模块通过 RS-232 串口与 PTZ 云台硬件通信，使用 Pelco-D 协议发送控制指令并接收角度反馈。该模块运行在独立子线程中，通过 Qt 信号槽实现跨线程安全调用。", bodyStyle)]),

      makePara([makeRun("Pelco-D 协议帧格式", { bold: true, size: 24 })], { spacing: { before: 100, after: 80 } }),

      makeAsciiDiagram(
`+------+---------+------+------+------+------+----------+
| 0xFF | Address | Cmd1 | Cmd2 | Data1| Data2| Checksum |
+------+---------+------+------+------+------+----------+
  1字节   1字节    1字节   1字节   1字节   1字节    1字节`),

      makePara([makeRun("Sync: 0xFF 固定帧头 | Address: 设备地址(1~255) | Checksum: (Address+Cmd1+Cmd2+Data1+Data2) & 0xFF", { italics: true, size: 20 })], { spacing: { after: 120 } }),

      makePara([makeRun("常用命令码", { bold: true, size: 24 })], { spacing: { before: 100, after: 80 } }),

      makeTable(
        ["Cmd1", "Cmd2", "Data1", "Data2", "功能"],
        [
          ["0x00", "0x00", "0x00", "0x00", "停止所有运动"],
          ["0x00", "0x02", "hSpeed", "vSpeed", "向右+向下"],
          ["0x00", "0x04", "hSpeed", "vSpeed", "向左+向上"],
          ["0x00", "0x08", "hSpeed", "vSpeed", "向上"],
          ["0x00", "0x10", "hSpeed", "vSpeed", "向下"],
          ["0x00", "0x07", "0x00", "presetId", "调用预置位"],
          ["0x00", "0x03", "0x00", "presetId", "设置预置位"],
          ["0x00", "0x4B", "pan_H", "pan_L", "水平绝对定位"],
          ["0x00", "0x4D", "tilt_H", "tilt_L", "俯仰绝对定位"],
          ["0x00", "0x51", "0x00", "0x00", "查询水平角度"],
          ["0x00", "0x53", "0x00", "0x00", "查询俯仰角度"],
        ],
        [900, 900, 1200, 1200, 5160]
      ),

      makePara([makeRun("方向命令码位定义 (Cmd2): Bit1(0x02)=Right | Bit2(0x04)=Left | Bit3(0x08)=Up | Bit4(0x10)=Down", { italics: true, size: 20 })], { spacing: { after: 120 } }),

      makePara([makeRun("角度查询回复解析", { bold: true, size: 24 })], { spacing: { before: 100, after: 80 } }),
      makeBulletPoint("Cmd2 = 0x59: 水平角度回传，angle = (DataH × 256 + DataL) / 100.0"),
      makeBulletPoint("Cmd2 = 0x5B: 俯仰角度回传，若 angle > 270° 则转换为 angle - 360.0"),

      makePara([makeRun("支持的操作", { bold: true, size: 24 })], { spacing: { before: 100, after: 80 } }),

      makeTable(
        ["操作", "方法", "说明"],
        [
          ["绝对定位", "moveTo(pan, tilt, speed)", "三步发送：设速度→Pan→Tilt，延时20ms/50ms"],
          ["相对运动", "moveDirection(cmd2, hSpeed, vSpeed)", "方向盘式实时控制，速度范围0-63"],
          ["停止", "stop()", "发送全零停止指令"],
          ["预置位", "callPreset/setPreset(presetId)", "调用/设置预置位"],
          ["角度查询", "queryAngle()", "查询水平+俯仰，延时50ms分两次"],
          ["自动查询", "startAutoQuery(intervalMs)", "定时器驱动，默认100ms（10Hz）"],
        ],
        [1800, 3000, 4560]
      ),

      new Paragraph({ children: [new PageBreak()] }),

      // 2.2 RGB相机接入
      makePara([makeRun("2.2 RGB相机接入", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makePara([makeRun("SDK接入流程", { bold: true, size: 24 })], { spacing: { before: 100, after: 80 } }),

      makeCodeBlock(
`1. UNIV_SDK_Init() — 全局初始化SDK
2. UNIV_SDK_SetLogLevel(2) — 设置日志级别
3. UNIV_SDK_SetExceptionCallBack(OnException) — 注册异常回调
4. UNIV_DEV_Active(&dev) — 激活设备
5. UNIV_DEV_Login(&dev, &userID) — 登录设备，获取userID
6. UNIV_DEV_GetConfig(userID, ...) — 获取设备信息
7. UNIV_DEV_RealPlay(userID, 0, MAIN, OnStreamData) — 开始预览
8. OnStreamData回调 — 接收H.264/H.265裸流数据`),

      makePara([makeRun("登录过程", { bold: true, size: 22 })], { spacing: { before: 100 } }),
      makePara([makeRun("使用 QtConcurrent::run 在后台线程异步执行，通过 QFutureWatcher<LoginResult> 在主线程接收结果，避免阻塞UI。", bodyStyle)]),

      makePara([makeRun("视频流解码管线", { bold: true, size: 24 })], { spacing: { before: 100, after: 80 } }),

      makeAsciiDiagram(
`SDK回调 (OnStreamData)
    │
    ▼ 深拷贝 QByteArray
ThreadSafeQueue<QByteArray> (maxSize=50)
    │
    ▼ waitAndPop()
DecodeThread::run() — 独立解码线程
    │
    ▼
VideoDecoder::decodeFrame(data, size)
    ├── avcodec_send_packet() — 送入解码器
    ├── avcodec_receive_frame() — 获取解码帧
    ├── sws_scale() — YUV → RGB24 转换
    └── 返回 QImage (Format_RGB32)
    │
    ▼ emit frameDecoded(QImage)
DeviceManager → MainWindow`),

      makePara([makeRun("支持的编码格式", { bold: true, size: 24 })], { spacing: { before: 100, after: 80 } }),

      makeTable(
        ["编码格式", "支持状态", "说明"],
        [
          ["H.264", "✅ 支持", "默认解码器 (AV_CODEC_ID_H264)"],
          ["H.265/HEVC", "⚠️ 需手动修改", "可将初始化参数改为 AV_CODEC_ID_HEVC"],
        ],
        [2000, 2000, 5360]
      ),

      new Paragraph({ children: [new PageBreak()] }),

      // 2.3 目标跟踪
      makePara([makeRun("2.3 目标跟踪（DSST+KF）", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makePara([makeRun("算法原理", { bold: true, size: 24 })], { spacing: { before: 100, after: 80 } }),
      makePara([makeRun("系统采用 DSST (Discriminative Scale Space Tracking) 算法，结合卡尔曼滤波实现鲁棒的目标跟踪。DSST 将跟踪问题分解为两个独立的子问题：", bodyStyle)]),
      makeBulletPoint("平移估计 — 基于灰度特征的频域相关滤波，确定目标中心位置"),
      makeBulletPoint("尺度估计 — 基于FHOG特征的多尺度相关滤波，确定目标大小变化"),

      makePara([makeRun("状态机（三态）", { bold: true, size: 24 })], { spacing: { before: 100, after: 80 } }),

      makeAsciiDiagram(
`                ┌─────────────────┐
                │   正常跟踪态     │
                │ PSR >= 0.75基线  │
                └────┬──────┬─────┘
                     │      │
                连续3帧│  PSR正常│
                低PSR  │      │
                     ▼      ▼
      ┌─────────────────┐  KF更新
      │  PSR下降过渡态    │
      │ 加速学习率×3      │
      └────┬────────────┘
           │
      连续3帧│
      低PSR  │
           ▼
      ┌─────────────────┐
      │   遮挡模式态      │
      │ 纯KF预测位置      │
      │ Q噪声增大9倍      │
      │ 每10帧尝试重检测   │
      └────┬────────────┘
           │
      PSR > 0.8基线│
           │      │
           ▼      ▼
      ┌─────────────────┐
      │  恢复 + 模板融合  │
      │ 0.6备份+0.4当前   │
      └─────────────────┘`),

      makeTable(
        ["状态", "触发条件", "行为"],
        [
          ["正常跟踪", "PSR ≥ 0.75×基线", "检测定位 + KF更新 + 模板更新(lr=0.025)"],
          ["过渡态", "1~2帧低PSR", "使用检测位置 + 加速学习率(lr×3=0.075)"],
          ["遮挡模式", "连续3帧低PSR", "纯KF预测 + Q增大9倍 + 每10帧重检测"],
        ],
        [1800, 2200, 5360]
      ),

      makePara([makeRun("关键参数说明", { bold: true, size: 24 })], { spacing: { before: 100, after: 80 } }),

      makeTable(
        ["参数", "默认值", "含义", "调优建议"],
        [
          ["m_padding", "3.0", "搜索区域padding倍数", "大目标用2.0，小目标用3.0~4.0"],
          ["m_lambda", "0.01", "频域正则化系数", "噪声大场景可增大到0.05"],
          ["m_learningRate", "0.025", "模板滑动平均学习率", "静态场景可降到0.01"],
          ["m_scaleStep", "1.02", "尺度步长（33个尺度）", "通常不需调整"],
          ["m_outputSigmaFactor", "1/16", "高斯标签sigma因子", "影响响应图尖锐度"],
        ],
        [2000, 1200, 2800, 3360]
      ),

      new Paragraph({ children: [new PageBreak()] }),

      // 2.4 PTZ自动跟踪闭环
      makePara([makeRun("2.4 PTZ自动跟踪闭环", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makePara([makeRun("脱靶量计算", { bold: true, size: 24 })], { spacing: { before: 100, after: 80 } }),

      makeCodeBlock(
`float centerX = frameWidth / 2.0f;     // 画面中心X
float centerY = frameHeight / 2.0f;    // 画面中心Y
float deltaX = targetCenterX - centerX; // X脱靶量（正值=目标偏右）
float deltaY = targetCenterY - centerY; // Y脱靶量（正值=目标偏下）`),

      makePara([makeRun("方向映射（负反馈）", { bold: true, size: 24 })], { spacing: { before: 100, after: 80 } }),

      makeTable(
        ["脱靶量", "云台动作", "Pelco-D Cmd2 位"],
        [
          ["deltaX > 0（目标在右）", "云台右转", "Bit 1 (0x02)"],
          ["deltaX < 0（目标在左）", "云台左转", "Bit 2 (0x04)"],
          ["deltaY > 0（目标在下）", "云台下转", "Bit 4 (0x10)"],
          ["deltaY < 0（目标在上）", "云台上转", "Bit 3 (0x08)"],
        ],
        [3000, 2500, 3860]
      ),

      makePara([makeRun("负反馈原理: 目标偏离画面中心 → 云台向目标方向转动 → 目标回到画面中心 → 脱靶量归零 → 停止运动", { italics: true, size: 20 })], { spacing: { after: 120 } }),

      makePara([makeRun("安全策略", { bold: true, size: 24 })], { spacing: { before: 100, after: 80 } }),

      makeTable(
        ["策略", "实现", "参数"],
        [
          ["死区", "脱靶量小于阈值时不产生运动", "默认20像素"],
          ["限速", "P控制器输出限制在 [0, maxSpeed]", "默认20 (范围0-63)"],
          ["遮挡停止", "目标被遮挡时发送(0,0,0,0)停止PTZ", "emit ptzControlDelta(0,0,0,0)"],
          ["参数校验", "qBound限制死区和速度在合法范围", "死区1-100，速度1-63"],
        ],
        [1800, 3800, 3760]
      ),

      new Paragraph({ children: [new PageBreak()] }),

      // 2.5-2.10 其他功能
      makePara([makeRun("2.5 镜头控制", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makePara([makeRun("采用 Worker-Thread 模式，LensWorker 在独立线程中执行 SDK 调用。", bodyStyle)]),

      makeTable(
        ["功能", "方法", "说明"],
        [
          ["变倍", "zoomIn/Out(speed, stop)", "控制镜头放大/缩小"],
          ["聚焦", "focusNear/Far(speed, stop)", "手动聚焦近/远"],
          ["光圈", "irisOpen/Close(speed, stop)", "手动光圈开/关"],
          ["聚焦模式", "setFocusMode(isManual)", "true=手动，false=自动"],
          ["一键聚焦", "onePushFocus()", "触发一次自动聚焦"],
        ],
        [1800, 3000, 4560]
      ),

      makePara([makeRun("2.6 图像抓拍", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makeTable(
        ["模式", "方法", "说明"],
        [
          ["单次抓拍", "captureOnce(quality, w, h)", "通过SDK获取JPEG数据"],
          ["定时抓拍", "startTimedCapture(interval, ...)", "QTimer驱动，按间隔重复抓拍"],
        ],
        [1800, 3000, 4560]
      ),

      makePara([makeRun("处理流程: SDK回调 → 深拷贝 → QtConcurrent后台线程 → JPEG解码 + 增强 + 保存", { italics: true, size: 20 })], { spacing: { after: 120 } }),

      makePara([makeRun("2.7 录像功能", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makePara([makeRun("转码式录像: SDK H.264流 → 解码RGB → YUV420P → H.264编码(libx264) → MP4封装", { bold: true, size: 22 })], { spacing: { before: 100 } }),

      makeTable(
        ["参数", "值"],
        [
          ["编码器", "libx264"],
          ["Preset", "ultrafast"],
          ["Tune", "zerolatency"],
          ["码率", "4 Mbps"],
          ["像素格式", "YUV420P"],
          ["GOP", "25帧"],
          ["Movflag", "+faststart（优化网络播放）"],
        ],
        [3000, 6360]
      ),

      makePara([makeRun("2.8 数据记录", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makePara([makeRun("录像期间以 10Hz 频率记录: 时间戳、帧序号、PTZ角度/速度、目标X/Y坐标、脱靶量、置信度(PSR)", bodyStyle)]),
      makePara([makeRun("CSV格式: 序号, 时间戳, 日期时间, 帧序号, 水平角度, 俯仰角度, 水平速度, 俯仰速度, 检测有效, 目标X, 目标Y, X脱靶量, Y脱靶量, 置信度", { italics: true, size: 20 })]),

      makePara([makeRun("2.9 OSD控制", h2Style)], { heading: HeadingLevel.HEADING_2 }),
      makeTable(
        ["功能", "方法", "说明"],
        [
          ["时间显示开关", "enableOSDTime(enable)", "控制时间戳叠加"],
          ["星期显示开关", "enableOSDWeek(enable)", "控制星期叠加"],
          ["位置设置", "setOSDPosition(x, y)", "设置时间显示坐标"],
          ["字体大小", "setOSDFontSize(size)", "0=16×16, 1=32×32, 2=48×48, 3=64×64"],
        ],
        [2000, 2500, 4860]
      ),

      makePara([makeRun("2.10 配置管理", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makePara([makeRun("存储路径: HKEY_CURRENT_USER\\Software\\RGB_PTZ\\RGB_PTZ_Integrated", { italics: true, size: 20 })], { spacing: { after: 120 } }),

      makeTable(
        ["配置项", "Key", "默认值", "说明"],
        [
          ["设备IP", "device/ip", "192.168.1.68", "相机网络地址"],
          ["用户名", "device/username", "admin", "登录用户名"],
          ["密码", "device/password", "nanhu315", "登录密码（明文存储）"],
          ["抓拍间隔", "capture/interval", "10", "定时抓拍间隔（秒）"],
          ["图片质量", "capture/quality", "80", "JPEG质量（1-100）"],
          ["保存路径", "capture/savePath", "./captures", "抓拍保存目录"],
        ],
        [1800, 2000, 2000, 3560]
      ),

      new Paragraph({ children: [new PageBreak()] }),

      // ===== 3. 架构设计 =====
      makePara([makeRun("3. 架构设计", h1Style)], { heading: HeadingLevel.HEADING_1 }),

      makePara([makeRun("3.1 分层架构图", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makeAsciiDiagram(
`┌─────────────────────────────────────────────────────────────┐
│                     表示层 (Presentation)                     │
│  ┌───────────────────────────────────────────────────────┐  │
│  │  MainWindow (QMainWindow)                              │  │
│  │  - UI事件处理 (按钮点击、鼠标框选)                       │  │
│  │  - 视频帧显示 (QLabel + 跟踪框叠加绘制)                  │  │
│  │  - 三大模块协调: PTZ / RGB相机 / 目标跟踪                │  │
│  │  - 状态反馈: 错误提示、跟踪状态、录像时长                │  │
│  └───────────────────────────────────────────────────────┘  │
├─────────────────────────────────────────────────────────────┤
│                    业务逻辑层 (Business Logic)                │
│  ┌──────────┐ ┌──────────┐ ┌──────────────────┐            │
│  │Tracking  │ │PTZCtrl   │ │  LensManager      │            │
│  │+ObjTrack │ │(Pelco-D) │ │ (SDK镜头控制)     │            │
│  │ [独立线程]│ │ [独立线程]│ │  [独立线程]        │            │
│  └────┬─────┘ └────┬─────┘ └────────┬─────────┘            │
│  ┌────┴─────┐ ┌────┴─────┐ ┌────────┴─────────┐            │
│  │DataRec   │ │CfgManager│ │  CaptureManager  │            │
│  │(CSV记录)  │ │(QSettings)│ │  +ImageProcessor │            │
│  └──────────┘ └──────────┘ └──────────────────┘            │
├─────────────────────────────────────────────────────────────┤
│                    数据访问层 (Data Access)                   │
│  ┌──────────┐ ┌──────────┐ ┌──────────────────┐            │
│  │DeviceMgr │ │VideoDec  │ │  RecordThread    │            │
│  │(SDK接入)  │ │(FFmpeg)  │ │ (FFmpeg H.264编码) │            │
│  │          │ │DecodeThd │ │  ThreadSafeQueue  │            │
│  └──────────┘ └──────────┘ └──────────────────┘            │
├─────────────────────────────────────────────────────────────┤
│                    基础设施层 (Infrastructure)                │
│  ┌──────────┐ ┌──────────┐ ┌──────────────────┐            │
│  │UNIV SDK  │ │  FFmpeg  │ │   QSerialPort    │            │
│  │(DLL库)   │ │ Libraries│ │ (Pelco-D协议传输)  │            │
│  └──────────┘ └──────────┘ └──────────────────┘            │
└─────────────────────────────────────────────────────────────┘`),

      makePara([makeRun("3.2 模块依赖关系", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makeAsciiDiagram(
`                     ┌─────────────┐
                     │  MainWindow  │
                     │  (UI中枢)    │
                     └──────┬──────┘
            ┌───────────────┼────────────────┐
            │               │                │
            ▼               ▼                ▼
     ┌────────────┐  ┌────────────┐  ┌────────────────┐
     │PTZController│  │DeviceMgr  │  │TrackingController│
     │ (串口线程)  │  │ (SDK管理)  │  │  (主线程)        │
     └────────────┘  └─────┬──────┘  └───────┬────────┘
                           │                  │
              ┌────────────┼────────┐         │
              ▼            ▼        ▼         ▼
     ┌──────────┐ ┌──────────┐ ┌──────┐ ┌────────────┐
     │DecodeThd │ │RecordThd │ │Lens  │ │ObjectTracker│
     │ (解码)   │ │ (编码)   │ │Mgr   │ │ (跟踪线程)   │
     └────┬─────┘ └────┬─────┘ └──────┘ └──────┬─────┘
          │             │                       │
          ▼             ▼                       ▼
     ┌──────────┐ ┌──────────┐           ┌──────────┐
     │VideoDec  │ │VideoDec  │           │ FFTUtils │
     │ (FFmpeg) │ │ (FFmpeg) │           │ (FFT/FHOG)│
     └──────────┘ └──────────┘           └──────────┘`),

      makePara([makeRun("依赖规则: 上层依赖下层，下层不依赖上层；跨层通信统一使用Qt信号槽（QueuedConnection）", { italics: true, size: 20 })], { spacing: { after: 120 } }),

      makePara([makeRun("3.3 核心设计决策", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makeTable(
        ["决策", "选择", "理由"],
        [
          ["线程模型", "Worker-Thread (moveToThread)", "避免QThread子类局限，事件循环自动处理"],
          ["跨线程通信", "Qt信号槽 (QueuedConnection)", "线程安全、自动排队、无需手动同步"],
          ["SDK回调处理", "静态C回调 + instance指针 + Mutex", "SDK C API不支持成员函数回调"],
          ["录像方案", "转码式（解码→再编码）", "确保可叠加OSD、进行图像处理后录制"],
          ["跟踪算法", "DSST + KF 自实现", "参考MATLAB，无外部依赖，便于调优"],
          ["FFT实现", "Cooley-Tukey 迭代版", "轻量级，适用于2的幂次尺寸"],
          ["配置存储", "QSettings (Windows注册表)", "跨平台兼容，无需额外配置文件"],
        ],
        [2000, 3000, 4360]
      ),

      new Paragraph({ children: [new PageBreak()] }),

      // ===== 4. 线程模型 =====
      makePara([makeRun("4. 线程模型", h1Style)], { heading: HeadingLevel.HEADING_1 }),

      makePara([makeRun("4.1 线程清单", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makeTable(
        ["线程名称", "运行对象", "模式", "生命周期"],
        [
          ["主线程(GUI)", "MainWindow, DeviceMgr等", "Qt主事件循环", "应用全程"],
          ["PTZ线程", "PTZController", "Worker-Thread", "连接时创建，断开时销毁"],
          ["解码线程", "DecodeThread", "QThread子类", "流开始时启动/停止"],
          ["录像线程", "RecordThread", "QThread子类", "录像开始时创建/销毁"],
          ["镜头工作线程", "LensWorker", "Worker-Thread", "LensManager生命周期内常驻"],
          ["跟踪工作线程", "ObjectTracker", "Worker-Thread", "跟踪启动时创建，停止时销毁"],
          ["登录后台线程", "匿名函数", "Qt线程池", "单次登录后回收"],
          ["图像处理线程", "匿名函数", "Qt线程池", "单次处理后回收"],
        ],
        [2000, 2500, 2200, 2660]
      ),

      makePara([makeRun("4.2 同步机制", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makeTable(
        ["同步原语", "所属类", "保护资源"],
        [
          ["QMutex m_mutex", "DeviceManager", "m_connected, m_streaming等状态"],
          ["QMutex s_instanceMutex", "DeviceMgr/CaptureMgr", "instance 静态指针"],
          ["QMutex m_mutex", "DataRecorder", "m_recording, m_dataPoints"],
          ["QMutex m_mutex", "LensManager", "m_userID, m_ready等"],
          ["QMutex + QWaitCondition", "ThreadSafeQueue<T>", "队列数据, 停止标志"],
        ],
        [2500, 2500, 4360]
      ),

      makePara([makeRun("4.3 线程安全约束", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makeTable(
        ["模块", "线程安全", "使用约束"],
        [
          ["PTZController", "✅ 是（信号槽）", "必须通过信号调用"],
          ["ObjectTracker", "❌ 否", "必须在独占线程中使用"],
          ["VideoDecoder", "❌ 否", "仅被DecodeThread/RecordThread独享"],
          ["LensManager", "✅ 是（mutex+信号）", "可从任意线程调用"],
          ["DataRecorder", "✅ 是（内部mutex）", "可从任意线程调用"],
          ["DeviceManager", "✅ 是（mutex+回调保护）", "可从任意线程调用"],
          ["FFTUtils", "✅ 是（无状态纯函数）", "可从任意线程调用"],
        ],
        [2000, 2200, 5160]
      ),

      makePara([makeRun("4.4 线程生命周期管理（安全停止顺序）", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makePara([makeRun("这是本项目最关键的线程安全约束之一。违反停止顺序将导致 use-after-free 崩溃。", { bold: true, color: "C00000", size: 22 })], { spacing: { before: 100, after: 120 } }),

      makeCodeBlock(
`// TrackingController 停止流程（最严格）
void TrackingController::stopTracking() {
    // 1: 断开所有信号连接
    if (m_tracker) disconnect(m_tracker, nullptr, nullptr, nullptr);
    // 2: 请求线程事件循环退出
    if (m_trackerThread) m_trackerThread->quit();
    // 3: 阻塞等待线程实际退出
    if (m_trackerThread) m_trackerThread->wait();
    // 4: 删除工作对象
    if (m_tracker) { delete m_tracker; m_tracker = nullptr; }
    // 5: 删除线程对象
    if (m_trackerThread) { delete m_trackerThread; m_trackerThread = nullptr; }
}`),

      makePara([makeRun("关键原则: 先断开信号 → 再请求退出 → 再等待完成 → 最后删除对象 → 静态回调保护（instance置nullptr）", { italics: true, size: 22 })], { spacing: { before: 100 } }),

      new Paragraph({ children: [new PageBreak()] }),

      // ===== 5. 数据流分析 =====
      makePara([makeRun("5. 数据流分析", h1Style)], { heading: HeadingLevel.HEADING_1 }),

      makePara([makeRun("5.1 视频预览数据流", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makeAsciiDiagram(
`┌─────────────┐
│  网络相机    │──H.264/H.265流──▶
└─────────────┘
                      │
                      ▼
              ┌───────────────┐
              │ UNIVision SDK │
              │  OnStreamData │◄── SDK内部回调线程
              │  (C静态回调)   │
              └───────┬───────┘
                      │ 深拷贝 QByteArray
                      ▼
              ┌───────────────┐
              │ThreadSafeQueue│◄── maxSize=50，满时丢弃最旧
              │  <QByteArray> │
              └───────┬───────┘
                      │ waitAndPop() 阻塞等待
                      ▼
              ┌───────────────┐
              │ DecodeThread  │◄── 独立解码线程
              │   .run()      │
              │ VideoDecoder  │
              │  decodeFrame()│──avcodec_send_packet
              │               │──avcodec_receive_frame
              │               │──sws_scale (YUV→RGB)
              └───────┬───────┘
                      │ emit frameDecoded(QImage)
                      ▼
              ┌───────────────┐
              │DeviceManager  │
              │onFrameDecoded()│
              └───────┬───────┘
                      │ emit frameReceived(QImage)
                      ▼
              ┌───────────────┐
              │  MainWindow   │◄── 主线程（GUI）
              │onFrameReceived│
              │ displayImage()│── QLabel显示
              │ 叠加跟踪框     │── QPainter绘制
              │ if (tracking):│──▶ 转发给TrackingController
              └───────────────┘`),

      makePara([makeRun("5.2 目标跟踪 + PTZ控制数据流", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makeAsciiDiagram(
`MainWindow (主线程)
  │
  ▼ onFrameReceived(QImage)
TrackingController::processFrame(frame)
  │
  │ QMetaObject::invokeMethod(QueuedConnection)
  │ 跨线程 → 跟踪工作线程
  ▼
┌─────────────────────────────────────┐
│     ObjectTracker (跟踪工作线程)     │
│                                     │
│ update(frame):                      │
│   ├── kfPredict()                   │
│   ├── getTranslationSample()        │
│   ├── computeResponse() (FFT相关)   │
│   ├── computePSR()                  │
│   ├── 尺度估计 (FHOG + 33尺度)      │
│   ├── 状态决策 (三态机)             │
│   └── 返回 TrackResult              │
│                                     │
│ emit trackingDone(result) ◄─────────┘
│ 跨线程 → 主线程
└─────────────────────────────────────┘
  │
  ▼ TrackingController [主线程回调]
computePTZControl(result):
  deltaX = targetCx - frameCenterX
  deltaY = targetCy - frameCenterY
  if (|delta| < deadZone): delta = 0
  speedX = min(Kp * |deltaX|, maxSpeed)
  │
  ▼ emit ptzControlDelta(deltaX, deltaY, speedX, speedY)
MainWindow::onPtzControlDelta(...)
  │ 脱靶量 → Pelco-D方向命令码:
  │   if (deltaX > 0): cmd2 |= 0x02  // 右
  │   if (deltaX < 0): cmd2 |= 0x04  // 左
  │   if (deltaY > 0): cmd2 |= 0x10  // 下
  │   if (deltaY < 0): cmd2 |= 0x08  // 上
  │
  ▼ emit ptzMoveDirection(cmd2, hSpeed, vSpeed)
  QueuedConnection → PTZ线程
┌─────────────────────────────────────┐
│       PTZController (PTZ线程)        │
│                                     │
│ moveDirection(cmd2, hSpeed, vSpeed) │
│   ├── 安全限幅: min(speed, 0x3F)   │
│   └── writeToSerial()              │
└───────────────┬─────────────────────┘
                │ 7字节 Pelco-D 帧
                ▼
        ┌───────────────┐
        │ PTZ 云台硬件   │◄── 转动到目标方向
        └───────────────┘`),

      makePara([makeRun("5.3 录像数据流", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makeAsciiDiagram(
`┌─────────────┐
│  网络相机    │──H.264流──▶
└─────────────┘
                      │
                      ▼
              ┌───────────────┐
              │ SDK回调        │
              │ OnStreamData  │
              └───────┬───────┘
                      │
              ┌───────┴───────┐
              ▼               ▼
     ┌──────────────┐ ┌──────────────┐
     │ DecodeThread  │ │ RecordThread │
     │ 队列(预览)    │ │ 队列(录像)    │
     └──────┬───────┘ └──────┬───────┘
            │                │
            ▼                ▼
     ┌──────────────┐ ┌──────────────┐
     │ 解码为QImage  │ │ 解码为QImage  │
     │ 显示到UI      │ │ (首帧获取分辨率)│
     └──────────────┘ └──────┬───────┘
                             │
                             ▼
                    ┌────────────────┐
                    │ sws_scale      │
                    │ RGB → YUV420P  │
                    └───────┬────────┘
                            │
                            ▼
                    ┌────────────────┐
                    │ avcodec_       │
                    │ encode_video   │
                    │ (H.264/libx264)│
                    └───────┬────────┘
                            │
                            ▼
                    ┌────────────────┐
                    │ 输出文件.mp4    │
                    └────────────────┘

同时:
DataRecorder ──10Hz定时采样──▶ m_dataPoints
                          停止录像时
                          exportToCsv() ──▶ 录像同目录/文件名.csv`),

      new Paragraph({ children: [new PageBreak()] }),

      // ===== 6. 核心算法剖析 =====
      makePara([makeRun("6. 核心算法剖析", h1Style)], { heading: HeadingLevel.HEADING_1 }),

      makePara([makeRun("6.1 DSST跟踪算法", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makePara([makeRun("平移滤波原理 — 频域相关滤波", { bold: true, size: 24 })], { spacing: { before: 100, after: 80 } }),

      makeCodeBlock(
`训练阶段:
1. 提取搜索区域灰度patch x (m_modelW × m_modelH)
2. 应用Hann窗: x_w = x ⊙ w
3. FFT: X_f = FFT2(x_w)
4. 高斯标签: y (中心为1的高斯分布) → FFT: Y_f
5. 计算滤波器:
   H_num = Y_f ⊙ conj(X_f)           // 分子
   H_den = Σ |X_f|²                  // 分母（逐元素能量和）
   H = H_num / (H_den + λ)           // 正则化除法

检测阶段:
1. 提取新一帧搜索区域 z
2. 应用Hann窗: z_w = z ⊙ w
3. FFT: Z_f = FFT2(z_w)
4. 计算响应: R = IFFT( Σ(H_num ⊙ Z_f) / (H_den + λ) )
5. 找峰值位置: (row, col) = argmax(R)
6. 转换为实际坐标

模型更新:
H_num = (1 - α) × H_num + α × H_num_new
H_den = (1 - α) × H_den + α × H_den_new
学习率 α = 0.025（正常）或 0.075（过渡态）`),

      makePara([makeRun("尺度滤波原理（FHOG + 33尺度）", { bold: true, size: 24 })], { spacing: { before: 100, after: 80 } }),

      makeCodeBlock(
`33个尺度因子: scale_step^(16-s), s=0..32
  即: [1.02^16, 1.02^15, ..., 1.0, ..., 1.02^-15, 1.02^-16]

对每个尺度 s:
  1. 裁剪 patch: 尺寸 = target_size × scale_factors[s]
  2. 双线性缩放到 scale_model_sz (最大面积512像素)
  3. 计算FHOG特征 (31维/cell)
  4. 展平: 所有cell求和 → 31维向量
  5. 应用Hann窗
结果: 31通道 × 33尺度的特征矩阵

尺度滤波:
1. 对每个通道 c: X_sf[c] = FFT(scaleSample[c])  // 1D FFT (33维)
2. 计算响应: response = IFFT( sum_num / (sum_den + λ) )
3. 最佳尺度索引: bestIdx = argmax(real(response))
4. 更新当前尺度: currentScale *= scale_factors[bestIdx]`),

      makePara([makeRun("模板更新策略", { bold: true, size: 24 })], { spacing: { before: 100, after: 80 } }),

      makeTable(
        ["场景", "策略", "学习率"],
        [
          ["正常跟踪", "滑动平均更新", "α = 0.025"],
          ["PSR下降过渡态", "加速外观适应", "α = min(0.025×3, 0.15) = 0.075"],
          ["遮挡模式", "停止更新", "α = 0（防止污染模型）"],
          ["恢复成功", "备份融合", "H = 0.6×备份 + 0.4×当前"],
        ],
        [2500, 3000, 3860]
      ),

      makePara([makeRun("6.2 卡尔曼滤波", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makePara([makeRun("状态向量: x = [px, py, vx, vy]^T（位置x, 位置y, 速度x, 速度y）", { bold: true, size: 22 })], { spacing: { before: 100 } }),

      makeCodeBlock(
`预测:
状态转移矩阵 A = [1 0 1 0]    (T=1帧间隔)
                 [0 1 0 1]
                 [0 0 1 0]
                 [0 0 0 1]

x_pred = A · x
P_pred = A · P · A^T + Q
P = (P + P')/2   // 对称化处理，防止浮点误差

更新:
观测矩阵 H = [1 0 0 0]
             [0 1 0 0]

新息 y = z - H·x_pred
新息协方差 S = H·P·H^T + R
卡尔曼增益 K = P·H^T·S^(-1)
x = x_pred + K·y
P = (I - K·H)·P

遮挡模式: Q增大9倍 (100 → 900)，纯KF预测不更新
速度平滑: x[2] = 0.6*x[2] + 0.4*(measX - prevX)`),

      makePara([makeRun("6.3 FFT实现", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makePara([makeRun("Cooley-Tukey 迭代版算法: 位反转重排 → 蝶形运算 → IFFT归一化。输入长度必须是2的幂次。", bodyStyle)]),
      makePara([makeRun("2D FFT: 行优先后列优先的行列分解法，时间复杂度 O(M·N·log(M·N))", bodyStyle)]),

      makePara([makeRun("6.4 FHOG特征提取", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makePara([makeRun("每个cell输出31维特征: 18维(CS) + 9维(CI) + 4维(Texture)", { bold: true, size: 22 })], { spacing: { before: 100 } }),

      makeTable(
        ["通道范围", "名称", "维数", "说明"],
        [
          ["0-17", "Contrast-Sensitive (CS)", "18", "9方向×2（不合并相反方向）"],
          ["18-26", "Contrast-Insensitive (CI)", "9", "9方向（合并相反方向）"],
          ["27-30", "Texture", "4", "2×2 block纹理能量"],
          ["合计", "", "31", "每cell输出"],
        ],
        [1500, 2500, 1200, 4160]
      ),

      new Paragraph({ children: [new PageBreak()] }),

      // ===== 7. 关键代码剖析 =====
      makePara([makeRun("7. 关键代码剖析", h1Style)], { heading: HeadingLevel.HEADING_1 }),

      makePara([makeRun("7.1 项目文件结构", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makeCodeBlock(
`RGB_PTZ_Integrated/
├── RGB_PTZ_Integrated.pro        # qmake项目文件
│   ├── Qt模块: core, gui, widgets, serialport, concurrent
│   └── 外部依赖: UNIVision SDK, FFmpeg 8.1
│
├── 入口与UI
│   ├── main.cpp                  # 程序入口（注册跨线程元类型）
│   ├── mainwindow.h/.cpp/.ui     # 主窗口（UI控制器）
│   └── ui_mainwindow.h           # Qt Designer自动生成
│
├── 数据模型
│   └── datatypes.h               # 共享数据结构（PTZData, DetectionData）
│
├── PTZ控制
│   └── ptzcontroller.h/.cpp      # Pelco-D串口控制器（独立线程）
│
├── 相机与视频
│   ├── devicemanager.h/.cpp      # 设备管理器（SDK接入中枢）
│   ├── videodecoder.h/.cpp       # FFmpeg解码器
│   ├── DecodeThread.h/.cpp       # 解码线程
│   ├── RecordThread.h/.cpp       # 录像线程（H.264编码）
│   └── ThreadSafeQueue.h         # 线程安全队列模板
│
├── 抓拍与图像
│   ├── capturemanager.h/.cpp     # 抓图管理器
│   └── imageprocessor.h/.cpp     # 图像处理器
│
├── 镜头控制
│   └── lensmanager.h/.cpp        # 镜头管理器（Worker-Thread）
│
├── 配置与记录
│   ├── configmanager.h/.cpp      # 配置管理器（QSettings）
│   └── datarecorder.h/.cpp       # 数据记录器（CSV）
│
├── 目标跟踪
│   ├── objecttracker.h/.cpp      # DSST+KF目标跟踪器
│   ├── trackingcontroller.h/.cpp # PTZ自动跟踪控制器
│   └── fftutils.h/.cpp           # FFT与FHOG工具库
│
└── 参考文件/
    ├── RGB_PTZ跟踪融合方案_v1.4.docx
    └── matlab跟踪算法参考文件/`),

      makePara([makeRun("7.2 核心类继承关系", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makeCodeBlock(
`QObject
  ├── MainWindow : QMainWindow
  ├── PTZController          → 内部: QSerialPort, QTimer
  ├── DeviceManager          → 持有: DecodeThread, RecordThread*, LensManager
  ├── DecodeThread : QThread → 内部: ThreadSafeQueue, VideoDecoder
  ├── RecordThread : QThread → 内部: ThreadSafeQueue, VideoDecoder, FFmpeg
  ├── CaptureManager         → 依赖: DeviceManager, ImageProcessor
  ├── ImageProcessor
  ├── LensManager            → 持有: LensWorker (独立QThread)
  ├── ConfigManager          → 内部: QSettings
  ├── DataRecorder           → 内部: QTimer, QVector<DataPoint>
  ├── TrackingController     → 持有: ObjectTracker (独立QThread)
  ├── ObjectTracker          → 内部: 平移/尺度滤波器, 卡尔曼滤波
  └── LensWorker : QObject

非QObject类:
  ├── VideoDecoder           (FFmpeg解码封装)
  └── ThreadSafeQueue<T>     (模板化线程安全队列)`),

      makePara([makeRun("7.3 关键设计模式", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makeTable(
        ["设计模式", "应用位置", "说明"],
        [
          ["Worker-Thread", "PTZController, LensWorker, ObjectTracker", "moveToThread()移入子线程，事件循环处理消息"],
          ["生产者-消费者", "DecodeThread, RecordThread", "ThreadSafeQueue解耦SDK回调与解码线程"],
          ["观察者模式", "Qt信号槽机制", "所有模块间通信统一通过信号槽"],
          ["状态机模式", "ObjectTracker(三态), TrackingController(五态)", "状态转换: 正常→过渡→遮挡→恢复"],
          ["单例模式（受限）", "DeviceManager::instance", "仅用于SDK静态回调访问"],
          ["策略模式", "PTZ控制方向映射", "脱靶量正负决定Pelco-D命令码位"],
          ["模板方法", "ThreadSafeQueue<T>", "模板化队列支持任意类型"],
        ],
        [2000, 3000, 4360]
      ),

      new Paragraph({ children: [new PageBreak()] }),

      // ===== 8. 开发指南 =====
      makePara([makeRun("8. 开发指南", h1Style)], { heading: HeadingLevel.HEADING_1 }),

      makePara([makeRun("8.1 环境依赖", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makeTable(
        ["依赖项", "最低版本", "获取方式"],
        [
          ["Qt", "Qt 6.10", "Qt官方安装器，需勾选Core/GUI/Widgets/SerialPort/Concurrent"],
          ["编译器", "MinGW 64-bit (GCC 13+)", "Qt安装器自带"],
          ["C++标准", "C++17", "项目.pro中配置"],
          ["FFmpeg", "8.1 (shared build)", "项目已自带 ffmpeg-8.1-full_build-shared/"],
          ["UNIVision SDK", "设备厂商提供", "需放置sdk/sdk.h和univisionsdk.dll"],
        ],
        [2000, 2500, 4860]
      ),

      makePara([makeRun("8.2 构建步骤", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makeNumberedPoint("打开 Qt Creator → 文件 → 打开文件或项目 → 选择 RGB_PTZ_Integrated.pro"),
      makeNumberedPoint("选择构建套件: Desktop Qt 6.10.1 MinGW 64-bit"),
      makeNumberedPoint("点击左下角锤子图标 (Ctrl+B) 构建"),

      makePara([makeRun("命令行构建: qmake RGB_PTZ_Integrated.pro && mingw32-make", { italics: true, size: 20 })], { spacing: { before: 100 } }),

      makePara([makeRun("8.3 运行方式", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makeCodeBlock(
`# 调试模式（输出 qDebug 日志）
cd build\\Desktop_Qt_6_10_1_MinGW_64_bit-Debug\\debug
.\\RGB_PTZ_Integrated.exe

# 发布模式（无 qDebug 输出）
cd build\\Desktop_Qt_6_10_1_MinGW_64_bit-Release\\release
.\\RGB_PTZ_Integrated.exe`),

      makePara([makeRun("运行DLL依赖: univisionsdk.dll + FFmpeg(avcodec-61, avformat-61, avutil-59, swscale-8, swresample-5) + Qt6系列DLL", { italics: true, size: 20 })], { spacing: { after: 120 } }),

      makePara([makeRun("8.4 调试技巧", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makeTable(
        ["场景", "技巧"],
        [
          ["串口调试", "连接PTZController::rawDataInout信号，监控收发原始Hex数据"],
          ["跟踪调试", "在ObjectTracker::update()中输出PSR、KF预测位置、状态切换"],
          ["视频流调试", "检查DecodeThread是否收到数据：m_dataQueue.size()"],
          ["性能分析", "ObjectTracker内部使用QElapsedTimer输出每帧处理耗时和FPS"],
          ["线程监控", "使用Qt Creator的调试器查看各线程调用栈"],
        ],
        [2000, 7360]
      ),

      makePara([makeRun("8.5 常见问题排查", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makeTable(
        ["问题", "可能原因", "排查方法"],
        [
          ["串口打开失败", "端口号错误、被占用", "检查设备管理器COM端口；确认9600波特率"],
          ["相机登录失败", "IP错误、网络不通", "ping相机IP；确认凭据"],
          ["视频不显示", "解码线程未启动", "检查startStream()是否成功；确认H.264编码"],
          ["跟踪不启动", "未正确框选目标", "确认setTarget()被调用；目标框至少10×10像素"],
          ["PTZ不响应", "串口未打开、地址不匹配", "检查isOpen()；用rawDataInout信号监控"],
          ["录像文件损坏", "编码参数不匹配", "检查编码器初始化；确认stopRecord()被正确调用"],
          ["内存泄漏", "线程未正确停止", "检查析构函数线程停止顺序"],
        ],
        [2000, 2000, 5360]
      ),

      new Paragraph({ children: [new PageBreak()] }),

      // ===== 9. 优化建议 =====
      makePara([makeRun("9. 优化建议", h1Style)], { heading: HeadingLevel.HEADING_1 }),

      makePara([makeRun("9.1 性能优化方向", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makeTable(
        ["优化方向", "当前状态", "建议方案", "预期收益"],
        [
          ["FFT性能", "纯C++实现", "引入FFTW或SIMD优化", "3-5x提速"],
          ["FHOG计算", "逐像素计算", "积分图优化", "2-3x提速"],
          ["视频解码", "软件解码", "启用硬件解码(D3D11VA/CUDA)", "降低CPU 30-50%"],
          ["录像方案", "转码式", "直接封装H.264裸流", "CPU降低80%"],
          ["图像传输", "QImage拷贝", "使用QSharedPointer共享", "减少内存拷贝"],
          ["跟踪帧率", "每帧完整计算", "跳帧策略或缩小ROI", "FPS提升2x"],
        ],
        [1800, 2000, 2800, 2760]
      ),

      makePara([makeRun("9.2 功能扩展建议", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makeTable(
        ["扩展方向", "描述", "难度"],
        [
          ["多目标跟踪", "扩展DSST支持多目标（匈牙利算法）", "高"],
          ["深度学习检测", "集成YOLO用于初始化和重检测", "中-高"],
          ["预置位巡航", "按时间/条件自动调用预置位序列", "低"],
          ["网络推流", "RTSP/RTMP推送到网络", "中"],
          ["多相机支持", "同时接入多台相机进行跟踪", "中"],
          ["报警联动", "目标丢失/恢复时触发报警", "低"],
          ["H.265支持", "动态选择解码器", "低"],
        ],
        [2000, 4500, 2860]
      ),

      makePara([makeRun("9.3 已知问题", h2Style)], { heading: HeadingLevel.HEADING_2 }),

      makeTable(
        ["问题", "严重度", "描述"],
        [
          ["解码器硬编码H264", "中", "DecodeThread始终使用AV_CODEC_ID_H264"],
          ["密码明文存储", "中", "ConfigManager明文存储密码"],
          ["录像转码开销大", "中", "RecordThread先解码再编码，CPU占用高"],
          ["PSR基线采集期间", "低", "前30帧基线未锁定时遮挡检测不生效"],
          ["FFT非2幂次不支持", "低", "fft1d要求输入长度为2的幂次"],
        ],
        [2500, 1500, 5360]
      ),

      new Paragraph({ children: [new PageBreak()] }),

      // ===== 10. 变更记录 =====
      makePara([makeRun("10. 变更记录", h1Style)], { heading: HeadingLevel.HEADING_1 }),

      makeTable(
        ["日期", "变更内容", "变更人"],
        [
          ["2026-06-09", "初始项目说明文档生成，基于当前代码状态全面分析所有源代码、头文件、配置文件", "AI代理"],
        ],
        [2000, 5360, 2000]
      ),

    ] // end children
  }] // end sections
});

// Write file
Packer.toBuffer(doc).then(buffer => {
  const outputPath = '项目说明/RGB_PTZ_Integrated项目说明.docx';
  fs.writeFileSync(outputPath, buffer);
  console.log('Document generated: ' + outputPath);
}).catch(err => {
  console.error('Error:', err);
  process.exit(1);
});
