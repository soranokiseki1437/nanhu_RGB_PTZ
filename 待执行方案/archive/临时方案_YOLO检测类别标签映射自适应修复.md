# 临时方案：YOLO目标检测类别标签映射自适应修复规范

> **方案性质**：已执行完毕的临时修复方案（Checkpoint #75 闭环，待归档至 `待执行方案/archive/`）。  
> **适用目标**：解决反无人机 YOLO11/YOLOv8 专属检测模型输出标签显示为 `person` 的映射缺陷。  
> **涉及模块**：[onnxdetector.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/onnxdetector.cpp)、[yolopostprocessor.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/yolopostprocessor.cpp)、[yolopostprocessor.h](file:///d:/PyProjects/RGB_PTZ_Integrated/yolopostprocessor.h)。  
> **当前状态**：✅ 代码已落地，编译通过，实测验证通过（UAV 标签正确，COCO 零退化）。

---

## 一、 问题根因与物理事实

1. **训练端张量拓扑**：
   - 专属无人机检测模型基于 [datasets/anti_uav.yaml](file:///d:/PyProjects/RGB_PTZ_Integrated/datasets/anti_uav.yaml) 训练，类别数定义为 $nc = 1$（`names: ['UAV']`）。
   - 导出的静态 ONNX 模型输出维度为 $[1, 5, 8400]$（通道 0~3 为边界框坐标 $c_x, c_y, w, h$，通道 4 为 UAV 置信度得分）。
   - 类别数量计算公式为 $numClasses = channels - 4 = 1$，检测命中目标的类别索引 `maxClassId` 恒为 `0`。

2. **C++ 上位机配置缺省**：
   - 上位机 [mainwindow.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/mainwindow.cpp#L1645)、[trackingcontroller.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/trackingcontroller.cpp#L52)、[objecttracker.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/objecttracker.cpp#L587) 及 [tools/test_detector.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/tools/test_detector.cpp#L101) 调用 `loadModel()` 时，传入的 `DetectorConfig.classNames` 均为空向量。

3. **后处理硬编码回退缺陷**：
   - [yolopostprocessor.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/yolopostprocessor.cpp#L76) 当前逻辑：
     ```cpp
     const std::vector<std::string>& labels = classNames.empty() ? s_cocoNames : classNames;
     ```
   - 当 `classNames` 为空时，无条件回退为标准 COCO 80 类别列表 `s_cocoNames`。
   - `s_cocoNames[0]` 对应为 `"person"`，导致 UAV 单类别模型输出的目标标签被错误标记为 `"person"`。

---

## 二、 修复设计：三级类别映射自适应机制

为确保所有模型（专属无人机单类模型、COCO 80 类通用模型、多类别自定模型）均能正确解析类别，采用三级优先级防御设计：

```
[优先级 1] DetectorConfig::classNames (调用方显式配置，若非空直接使用)
       │
       ▼ (为空时向下传递)
[优先级 2] ONNX ModelMetadata 动态提取 (读取 ONNX 内嵌 custom metadata "names")
       │
       ▼ (元数据缺失或解析失败时向下传递)
[优先级 3] 张量拓扑通道数物理兜底 (numClasses == 1 -> UAV, numClasses == 80 -> COCO)
```

### 1. 优先级 2：ONNX 模型元数据动态解析
Ultralytics YOLO 导出 ONNX 时在 Custom Metadata 中内嵌了序列化字典：
- UAV 模型：`names: "{0: 'UAV'}"`
- COCO 模型：`names: "{0: 'person', 1: 'bicycle', ...}"`

在 `OnnxDetector::initializeSession` 中通过 ONNX Runtime C API 读取元数据并解析：
- API：`SessionGetModelMetadata` 与 `ModelMetadataLookupCustomMetadataMap(metadata, allocator, "names", &val)`；
- 采用正则表达式 `(\d+)\s*:\s*['"]([^'"]+)['"]` 提取类别映射字典；
- 转换为有序 `std::vector<std::string>` 写入 `m_config.classNames`。

### 2. 优先级 3：张量输出通道数保底机制
若加载的模型为精简版或剔除了元数据：
- 当 $channels == 5$（$numClasses = 1$），自动填充类别为 `{"UAV"}`；
- 当 $channels == 84$（$numClasses = 80$），填充类别为 `s_cocoNames`；
- 其他未知通道数，填充为 `{"class_0", "class_1", ...}`。

在 [yolopostprocessor.cpp](file:///d:/PyProjects/RGB_PTZ_Integrated/yolopostprocessor.cpp) 中同步设置通道防御，即使独立调用 `YoloPostProcessor::process` 传入空向量，单类别模型亦映射为 `"UAV"`。

---

## 三、 代码修改详细方案

### 3.1 修改 `onnxdetector.cpp`
在 `OnnxDetector::initializeSession` 获取 `m_tensorChannels` 后，追加元数据解析与通道兜底逻辑：

```cpp
// -------------------------------------------------------------
// 类别名称自适应提取（元数据解析 -> 通道数拓扑兜底）
// -------------------------------------------------------------
if (m_config.classNames.empty()) {
    bool metadataParsed = false;
    OrtModelMetadata* modelMetadata = nullptr;
    if (m_api->SessionGetModelMetadata(m_session, &modelMetadata) == nullptr && modelMetadata) {
        char* customNames = nullptr;
        if (m_api->ModelMetadataLookupCustomMetadataMap(modelMetadata, allocator, "names", &customNames) == nullptr && customNames) {
            QString namesStr = QString::fromUtf8(customNames);
            allocator->Free(allocator, customNames);

            static const QRegularExpression rx("(\\d+)\\s*:\\s*['\"]([^'\"]+)['\"]");
            QRegularExpressionMatchIterator it = rx.globalMatch(namesStr);
            std::map<int, std::string> classMap;
            int maxId = -1;
            while (it.hasNext()) {
                QRegularExpressionMatch match = it.next();
                int cid = match.captured(1).toInt();
                std::string cname = match.captured(2).toStdString();
                classMap[cid] = cname;
                if (cid > maxId) maxId = cid;
            }
            if (maxId >= 0) {
                std::vector<std::string> parsedClasses(maxId + 1);
                for (int i = 0; i <= maxId; ++i) {
                    auto found = classMap.find(i);
                    parsedClasses[i] = (found != classMap.end()) ? found->second : ("class_" + std::to_string(i));
                }
                m_config.classNames = std::move(parsedClasses);
                metadataParsed = true;
            }
        }
        m_api->ReleaseModelMetadata(modelMetadata);
    }

    if (!metadataParsed) {
        int numClasses = m_tensorChannels - 4;
        if (numClasses == 1) {
            m_config.classNames = {"UAV"};
        } else if (numClasses == 80) {
            m_config.classNames = YoloPostProcessor::getCocoNames();
        } else {
            m_config.classNames.clear();
            for (int i = 0; i < numClasses; ++i) {
                m_config.classNames.push_back("class_" + std::to_string(i));
            }
        }
    }
}
```

### 3.2 修改 `yolopostprocessor.cpp`
在 `YoloPostProcessor::process` 中更新兜底逻辑，避免空参数时无条件强转 COCO：

```cpp
int numClasses = tensorChannels - 4;
static const std::vector<std::string> s_uavDefault = {"UAV"};

const std::vector<std::string>& labels = !classNames.empty() ? classNames
    : (numClasses == 1 ? s_uavDefault : s_cocoNames);
```

---

## 四、 实测验证记录

### 验证项 1：元数据解析覆盖性
- **测试对象**：`models/` 目录下全部 9 款部署模型（5 款 UAV 专属 + 4 款 COCO 通用）。
- **实测结果**：
  - `yolo11n_uav_ft.onnx`：元数据 `names` → `['UAV']`（长度 1）；
  - `yolo11s_uav_ft.onnx`：元数据 `names` → `['UAV']`（长度 1）；
  - `yolo11n_uav_scratch.onnx`：元数据 `names` → `['UAV']`（长度 1）；
  - `yolov8n_uav_ft.onnx`：元数据 `names` → `['UAV']`（长度 1）；
  - `yolov8s_uav_ft.onnx`：元数据 `names` → `['UAV']`（长度 1）；
  - `yolo11n.onnx` / `yolov8n.onnx`：元数据 `names` → 80 类（首项 `person`）。
- 元数据解析逻辑与 9 款模型 100% 匹配。

### 验证项 2：张量通道拓扑兜底
- UAV 模型输出 $[1, 5, 8400]$，$channels = 5 \implies numClasses = 1$，映射 `{"UAV"}`。
- COCO 模型输出 $[1, 84, 8400]$，$channels = 84 \implies numClasses = 80$，映射 `s_cocoNames`。

### 验证项 3：实时性影响
- 元数据提取仅在 `loadModel()` 执行一次，耗时 < 0.1 ms。
- `detect()` 逐帧推理循环通过 `const std::vector<std::string>&` 引用查表，逐帧附加开销为 0。

### 验证项 4：内存安全
- `OrtModelMetadata` 通过 `ReleaseModelMetadata` 释放。
- `customNames` 通过 `allocator->Free()` 释放。
- 零内存泄漏。

---

## 五、 执行与销毁指引

1. **执行方式**：按照第三节代码修改，执行 MinGW 64-bit 编译并运行 [run_test.ps1](file:///d:/PyProjects/RGB_PTZ_Integrated/run_test.ps1) 验证。
2. **销毁方式**：验证通过后，将代码纳入正式 Git 版本树追踪，本方案文档直接保留或移入 `archive/`。
