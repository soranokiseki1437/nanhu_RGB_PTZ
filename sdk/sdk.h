#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#ifndef UNIV_API
#define UNIV_API __declspec(dllimport)
#endif
#define UNIV_CALL __stdcall
#define UNIV_CALLBACK __stdcall
#else
#define UNIV_API __attribute__((visibility("default")))
#define UNIV_CALL
#define UNIV_CALLBACK
#endif

/************* 常量定义 *************/
#define MAX_POLYGON_POINT_NUM 8

/**
 * @brief SDK错误码 SDK接口返回int32_t类型值
 *
 */
typedef enum {
    UNKNOWN = -1,         /*!< 未知错误 */
    SUCCESS,              /*!< 成功 */
    UNIV_ERR_NOINIT,      /*!< SDK没有初始化 */
    UNIV_ERR_NOLOGIN,     /*!< 尚未登录 */
    UNIV_ERR_UID_INVALID, /*!< UID不合法 */
    UNIV_ERR_PARAMETERS,  /*!< 参数错误 */
    UNIV_ERR_UNSUPPORTED, /*!< 功能不支持 */
    UNIV_ERR_EXIST,       /*!< 禁止再次执行相同操作 */
    UNIV_ERR_TIMEOUT,     /*!< 请求超时 */
} UNIV_ERROR_CODE;

/**
 * @brief 异常消息code
 *
 */
typedef enum {
    EXCEPTION_KEEP_ALIVE,    /*!< 心跳失败 */
    EXCEPTION_SESSION_CLOSE, /*!< 会话断开，预览/事件订阅等 */
} UNIV_EXCEPTION_CODE;

/************* 数据结构定义 *************/
#pragma pack(push)
#pragma pack(1) /*!< 强制单字节对齐 */

typedef struct {
    int32_t X;
    int32_t Y;
} COORDINATE;

typedef struct {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
} RECT_;

typedef struct {
    uint8_t num;                           /*!< 有效点个数 */
    COORDINATE pos[MAX_POLYGON_POINT_NUM]; /*!< 多边形边界点 */
} POLYGON;

typedef struct {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} TIMEPOINT;

typedef struct {
    TIMEPOINT begin;
    TIMEPOINT end;
} TIMERANGE;

/************* SDK信息相关接口 *************/

/**
 * @brief 查询SDK版本信息
 *
 * @return const char*
 */
UNIV_API const char* UNIV_CALL UNIV_SDK_GetVersion(void);

/**
 * @brief 设置打印输出级别
 *
 * @param level 范围[0-ALERT, 1-ERROR, 2-WARN, 3-INFO, 4-DEBUG, 5-TRACE]，默认WARN
 */
UNIV_API void UNIV_CALL UNIV_SDK_SetLogLevel(uint8_t level);

typedef void(UNIV_CALLBACK* UNIV_LogCallBack)(uint8_t level, const char* file, int line, const char* func, const char* msg, void* userData);

/**
 * @brief 设置日志输出回调，默认输出到控制台
 *
 * @param cb 回调函数
 * @param userData 自定义数据
 */
UNIV_API void UNIV_CALL UNIV_SDK_SetLogCallback(UNIV_LogCallBack cb, void* userData);

/**
 * @brief 初始化SDK
 *
 * @return UNIV_ERROR_CODE
 */
UNIV_API int32_t UNIV_CALL UNIV_SDK_Init(void);

/**
 * @brief 释放资源
 *
 */
UNIV_API void UNIV_CALL UNIV_SDK_Cleanup(void);

/**
 * @brief 异常消息回调, 发生后，需要手动调用对应会话的关闭/注销接口
 *
 * @param code UNIV_EXCEPTION_CODE
 * @param userID 登录返回的userID
 */
typedef void(UNIV_CALLBACK* UNIV_ExceptionCallBack)(uint32_t code, uint64_t userID);

/**
 * @brief 设置异常消息回调
 *
 * @param cbExceptionCallBack 异常回调函数
 */
UNIV_API void UNIV_CALL UNIV_SDK_SetExceptionCallBack(UNIV_ExceptionCallBack cbExceptionCallBack);

/************* 用户登录相关接口 *************/

/**
 * @brief 登录参数
 *
 */
typedef struct {
    char ip[18];       /*!< 设备IP地址，仅支持IPvV4 */
    uint16_t port;     /*!< 设备通信端口 */
    char username[20]; /*!< 登录账户，utf-8编码  */
    char password[20]; /*!< 登录密码，utf-8编码 */
} UNIV_DEV_LOGIN_PARAM;

/**
 * @brief 设备未初始化时，需要激活设备
 *
 * @param pLoginInfo 账户admin，密码自定义
 * @return UNIV_ERROR_CODE
 */
UNIV_API int32_t UNIV_CALL UNIV_DEV_Activate(UNIV_DEV_LOGIN_PARAM* pLoginInfo);

/**
 * @brief 登录设备
 *
 * @param pLoginInfo 登录参数
 * @param pUserID 成功返回userid
 * @return UNIV_ERROR_CODE
 */
UNIV_API int32_t UNIV_CALL UNIV_DEV_Login(UNIV_DEV_LOGIN_PARAM* pLoginInfo, uint64_t* pUserID);

/**
 * @brief 用户注销，释放设备资源
 *
 * @param userID 登录产生的userid
 * @return UNIV_ERROR_CODE
 */
UNIV_API int32_t UNIV_CALL UNIV_DEV_Logout(uint64_t userID);

/**
 * @brief 设备系统信息
 *
 */
typedef struct {
    char deviceName[32];    /*!< 设备名称 */
    char deviceModel[32];   /*!< 设备型号 */
    char serialNumber[48];  /*!< 序列号 */
    char deviceVersion[32]; /*!< 设备版本 */
    char mcuVersion[32];    /*!< 云台版本 */
    char buildTime[32];     /*!< 编译日期 */
    char startTime[32];     /*!< 启动时间 */
} UNIV_DEV_DEVICE_INFO_PARAM;

typedef enum {
    DEVICE_SOFTWARE_ABILITY,
} DEVICE_CAPABILITY_TYPE;

/**
 * @brief 设备能力参数
 *
 */
typedef struct {
    uint8_t aiType; /*!< AI类型：0-普通相机，1-人脸相机，2-全结构化相机 */
} UNIV_DEV_CAPABILITY_SOFTWARE;

/**
 * @brief 获取设备能力集
 *
 * @param userID 登录产生的userid
 * @param code 能力类型 DEVICE_CAPABILITY_TYPE
 * @param pData 数据内容
 * @param dataSize 数据长度
 * @return UNIV_ERROR_CODE
 */
UNIV_API int32_t UNIV_CALL UNIV_DEV_GetCapability(uint64_t userID, DEVICE_CAPABILITY_TYPE code, void* pData, uint32_t dataSize);

/************* 设备维护相关接口 *************/

/**
 * @brief 恢复默认设置
 *
 * @param userID 登录产生的userid
 * @param mode 0-简单恢复，1-完全恢复
 * @return UNIV_ERROR_CODE
 */
UNIV_API int32_t UNIV_CALL UNIV_DEV_FactoryReset(uint64_t userID, uint8_t mode);

/**
 * @brief 重启设备
 *
 * @param userID 登录产生的userid
 * @return UNIV_ERROR_CODE
 */
UNIV_API int32_t UNIV_CALL UNIV_DEV_Reboot(uint64_t userID);

/************* 时间设置参数 *************/

/** 时区序号
    0	UTC
    1	UTC+1
    2	UTC+2
    3	UTC+3
    4	UTC+3:30
    5	UTC+4
    6	UTC+4:30
    7	UTC+5
    8	UTC+5:30
    9	UTC+5:45
    10	UTC+6
    11	UTC+6:30
    12	UTC+7
    13	UTC+8
    14	UTC+9
    15	UTC+9:30
    16	UTC+10
    17	UTC+11
    18	UTC+12
    19	UTC+13
    20	UTC-1
    21	UTC-2
    22	UTC-3
    23	UTC-3:30
    24	UTC-4
    25	UTC-5
    26	UTC-6
    27	UTC-7
    28	UTC-8
    29  UTC-9
    30	UTC-10
    31	UTC-11
    32	UTC-12
**/

/**
 * @brief NTP校时参数
 *
 */
typedef struct {
    uint8_t enable;   /*!< NTP校时开关 */
    char server[64];  /*!< 服务器地址 */
    uint16_t port;    /*!< 服务器端口 */
    uint8_t timeZone; /*!< 时区序号 */
    uint32_t period;  /*!< 间隔，单位分钟 */
} UNIV_DEV_NTP_PARAM;

/**
 * @brief 时间参数
 *
 */
typedef struct {
    uint8_t timeZone; /*!< 时区序号 */
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} UNIV_DEV_TIME_PARAM;

/************* 远程升级相关接口 *************/

/**
 * @brief 升级参数
 *
 */
typedef struct {
    uint8_t type;   /*!< 升级类型：0-机芯升级，1-云台升级，2-热像升级 */
    char* fileName; /*!< 升级文件路径 */
} UNIV_DEV_UPGRADE_PARAM;

/**
 * @brief 远程升级，升级成功后自动重启
 *
 * @param userID 登录产生的userid
 * @param pUpgradeInfo 升级参数，指定类型和文件地址
 * @param pUpgradeHandle 成功后返回升级句柄
 * @return UNIV_ERROR_CODE
 */
UNIV_API int32_t UNIV_CALL UNIV_DEV_Upgrade(uint64_t userID, UNIV_DEV_UPGRADE_PARAM* pUpgradeInfo, uint64_t* pUpgradeHandle);

/**
 * @brief 升级状态信息
 *
 */
typedef struct {
    int32_t state;   /*!< 当前升级状态：0-准备中，1-正在传输，2-传输失败，3-传输取消，4-准备升级，5-数据错误，6-正在升级，7-升级成功，8-升级取消，9-升级失败 */
    int32_t percent; /*!< 升级进度[0..100] */
} UNIV_DEV_UPGRADE_STATE;

/**
 * @brief 获取远程升级的状态信息
 *
 * @param upgradeHandle Upgrade返回的句柄
 * @param pStateInfo 状态信息
 * @return UNIV_ERROR_CODE
 */
UNIV_API int32_t UNIV_CALL UNIV_DEV_GetUpgradeState(uint64_t upgradeHandle, UNIV_DEV_UPGRADE_STATE* pStateInfo);

/**
 * @brief 关闭远程升级句柄，释放资源
 *
 * @param upgradeHandle Upgrade返回的句柄
 * @return UNIV_ERROR_CODE
 */
UNIV_API int32_t UNIV_CALL UNIV_DEV_CloseUpgrade(uint64_t upgradeHandle);

/************* 预览相关接口 *************/

/**
 * @brief 码流类型
 *
 */
typedef enum {
    MAIN = 0, /*!< 主码流 */
    EXTRA1,   /*!< 子码流 */
    EXTRA2,   /*!< 第三码流 */
    STREAM_INVALID
} STREAM_TYPE;

/**
 * @brief 音视频流数据回调
 *
 * @param handle 对应句柄
 * @param dataType 数据类型：视频帧 'I'、'P'，音频帧 'A'，JPEG图片'J'
 * @param pData 数据指针地址
 * @param dataSize 数据长度
 */
typedef void(UNIV_CALLBACK* UNIV_RealDataCallBack)(uint64_t handle, uint8_t dataType, void* pData, uint32_t dataSize);

/**
 * @brief 开启预览取流
 *
 * @param userID 登录产生的userid
 * @param channel 设备通道，一般取值 0
 * @param streamType 码流类型 STREAM_TYPE
 * @param pfnStreamDataCallBack 数据流回调，dataType取值 'I' 'P' 'A'，区分视频音频数据
 * @param pPlayHandle 成功后返回播放句柄
 * @return UNIV_ERROR_CODE
 */
UNIV_API int32_t UNIV_CALL UNIV_DEV_RealPlay(uint64_t userID, uint8_t channel, uint8_t streamType, UNIV_RealDataCallBack pfnStreamDataCallBack, uint64_t* pPlayHandle);

/**
 * @brief 停止实时预览，释放资源
 *
 * @param playHandle 播放句柄
 * @return UNIV_ERROR_CODE
 */
UNIV_API int32_t UNIV_CALL UNIV_DEV_StopRealPlay(uint64_t playHandle);

/**
 * @brief 开启语音对讲
 *
 * @param userID 用户ID
 * @param pfnDataCallBack 回调函数指针
 * @param pTalkHandle 成功返回句柄
 * @return UNIV_ERROR_CODE
 */
UNIV_API int32_t UNIV_CALL UNIV_DEV_StartTalkback(uint64_t userID, UNIV_RealDataCallBack pfnDataCallBack, uint64_t* pTalkHandle);

/**
 * @brief 发送语音数据，音频格式需要与相机音频编码格式相同
 *        相机协议问题：AAC格式数据前需要有4个字节的数据长度，小端字节序；其他格式不需要
 *
 * @param talkHandle 语音对讲句柄
 * @param pData 语音数据
 * @param dataSize 数据长度
 * @return UNIV_ERROR_CODE
 */
UNIV_API int32_t UNIV_CALL UNIV_DEV_TalkbackSend(uint64_t talkHandle, void* pData, uint32_t dataSize);

/**
 * @brief 关闭语音对讲
 *
 * @param talkHandle 语音对讲句柄
 * @return UNIV_ERROR_CODE
 */
UNIV_API int32_t UNIV_CALL UNIV_DEV_StopTalkback(uint64_t talkHandle);

/************* 抓图相关接口 *************/

typedef struct {
    uint64_t pts;
    uint16_t width;
    uint16_t height;
    uint32_t dataSize;
    void* data[0];
} UNIV_DEV_SNAP_DATA;

/**
 * @brief 开启抓图，智能化设备才能使用
 *
 * @param userID 登录产生的userid
 * @param channel 设备通道，一般取值 0
 * @param pfnSnapDataCallBack 数据流回调 pData: UNIV_DEV_SNAP_DATA, dataSize: sizeof(UNIV_DEV_SNAP_DATA) + jpeg数据长度
 * @param pSnapHandle 成功后返回抓图句柄
 * @return UNIV_ERROR_CODE
 */
UNIV_API int32_t UNIV_CALL UNIV_DEV_StartSnap(uint64_t userID, uint8_t channel, UNIV_RealDataCallBack pfnSnapDataCallBack, uint64_t* pSnapHandle);

/**
 * @brief 停止抓图
 *
 * @param snapHandle 抓图句柄
 * @return UNIV_ERROR_CODE
 */
UNIV_API int32_t UNIV_CALL UNIV_DEV_StopSnap(uint64_t snapHandle);

/**
 * @brief 单次抓拍图片，所有设备都可使用
 *
 * @param userID 登录产生的userid
 * @param channel 设备通道，一般取值 0，部分设备固件有问题，需要取值1
 * @param pfnSnapDataCallBack 抓图数据回调 pData: UNIV_DEV_SNAP_DATA, dataSize: sizeof(UNIV_DEV_SNAP_DATA) + jpeg数据长度
 * @param quality 图片质量 1-99
 * @param width 图片宽，如果为0，采用主码流分辨率的宽高
 * @param height 图片高，如果为0，采用主码流分辨率的宽高
 * @return UNIV_ERROR_CODE
 */
UNIV_API int32_t UNIV_CALL UNIV_DEV_SnapOnce(uint64_t userID, uint8_t channel, UNIV_RealDataCallBack pfnSnapDataCallBack, uint8_t quality, uint16_t width, uint16_t height);

/************* 云台相关接口 *************/

/**
 * @brief 云台控制命令
 *
 */
typedef enum {
    // 普通云台操作
    ZOOM_IN,    /*!< 焦距变大(倍率变大) */
    ZOOM_OUT,   /*!< 焦距变小(倍率变小) */
    FOCUS_NEAR, /*!< 焦点前调 */
    FOCUS_FAR,  /*!< 焦点后调 */
    IRIS_OPEN,  /*!< 光圈扩大 */
    IRIS_CLOSE, /*!< 光圈缩小 */
    TILT_UP,    /*!< 云台上仰 */
    TILT_DOWN,  /*!< 云台下俯 */
    PAN_LEFT,   /*!< 云台左转 */
    PAN_RIGHT,  /*!< 云台右转 */
    UP_LEFT,    /*!< 云台上仰和左转 */
    UP_RIGHT,   /*!< 云台上仰和右转 */
    DOWN_LEFT,  /*!< 云台下俯和左转 */
    DOWN_RIGHT, /*!< 云台下俯和右转 */
    PAN_AUTO,   /*!< 云台左右自动扫描 */
    // 外置设备开关
    LIGHT_PWRON,  /*!< 接通灯光电源 */
    WIPER_PWRON,  /*!< 接通雨刷开关 */
    FAN_PWRON,    /*!< 接通风扇开关 */
    DEFOG_PWRON,  /*!< 接通除雾开关 */
    HEATER_PWRON, /*!< 接通加热器开关 */
    // 一键操作
    AUTOMATIC_TRACK,   /*!< 自动跟踪 */
    LENS_INITIALIZE,   /*!< 镜头初始化 */
    FOCUS_ONEPUSH,     /*!< 辅助聚焦 */
    ONEKEY_PATROL,     /*!< 一键巡航 */
    ONEKEY_PARKACTION, /*!< 一键守望 */
    PT_ZERO,           /*!< 云台归零 */
    PT_REBOOT,         /*!< 云台重启 */
    // 预置点
    PRESET_SET,  /*!< 设置预置点 */
    PRESET_CLR,  /*!< 清除预置点 */
    PRESET_GOTO, /*!< 转到预置点 */
    // 巡航，设置路径点使用setConfig
    CRUISE_RUN, /*!< 开始巡航 */
    // 花样扫描
    TRACK_RECORD, /*!< 开始记录轨迹 */
    TRACK_RUN,    /*!< 开始轨迹 */
    // 线性扫描
    LINEARSWEEP_RUN,        /*!< 开始线性扫描 */
    LINEARSWEEP_LEFTLIMIT,  /*!< 设置左边界 */
    LINEARSWEEP_RIGHTLIMIT, /*!< 设置右边界 */
} UNIV_PTZ_COMMAND;

/**
 * @brief 云台控制接口
 *
 * @param userID 用户ID
 * @param command 云台控制命令
 * @param index 对应序号：预置点序号[1,255]，巡航路径[1,8]，花样扫描[1]，线性扫描[1,4]
 * @param speed 云台速度：普通操作[1,100]，线性扫描[1,8]
 * @param stop 云台停止动作或开始动作：0-开始，1-停止
 * @return UNIV_ERROR_CODE
 */
UNIV_API int32_t UNIV_CALL UNIV_DEV_PTZControl(uint64_t userID, UNIV_PTZ_COMMAND command, uint8_t index, uint8_t speed, uint8_t stop);

/***************** 音视频编码参数 *****************/

/**
 * @brief 视频分辨率，参照设备网页上，主码流子码流三码流的分辨率参数范围，其他非法值
 *
 */
typedef struct {
    uint16_t width;
    uint16_t height;
} UNIV_DEV_VIDEO_RESOLUTION;

/**
 * @brief 视频编码
 *
 */
typedef struct {
    uint8_t stream;                       /*!< 码流类型：0-主码流，1-子码流，2-子码流2 */
    uint8_t streamType;                   /*!< 码流类型：0-视频流，1-复合流 */
    uint8_t videoEncType;                 /*!< 视频编码类型：0-h264，1-MPEG4，2-MJPEG，3-h265 */
    uint8_t videoEncH264Complexity;       /*!< 视频编码复杂度(h264可用)：0-低，1-中，2-高 */
    uint8_t enableSvc;                    /*!< SVC功能(h264可用)：0-不启用，1-启用，2-自动 */
    uint8_t videoEncH265Complexity;       /*!< 视频编码复杂度(h264可用)：0-低，1-中，2-高 */
    UNIV_DEV_VIDEO_RESOLUTION resolution; /*!< 分辨率 */
    uint8_t videoFrameRate;               /*!< 视频帧率：1,2,4,6,8,10,12,15,16,18,20,22,25 */
    uint16_t intervalFrameI;              /*!< I帧间隔，[1,400] */
    uint16_t videoBitrate;                /*!< 视频码率：32,64,128,256,512,1024,2048,4096,6144,8192,16384 */
    uint8_t bitrateType;                  /*!< 码率类型：0-定码率，1-变码率 */
    uint8_t picQuality;                   /*!< 图像质量(变码率可用)：[1,5] */
    uint8_t streamSmooth;                 /*!< 码流平滑，[1,100] 清晰<->平滑 */
} UNIV_DEV_VIDEO_ENC_PARAM;

/**
 * @brief 音频编码
 *
 */
typedef struct {
    uint8_t audioEncType;      /*!< 音频编码类型：0-G722，1-G711_U，2-G711_A，5-MP2L2，6-G726，7-AAC，8-PCM */
    uint8_t audioBitRate;      /*!< 音频码率：0- 默认，1- 8Kbps，2- 16Kbps，3- 32Kbps，4- 64Kbps，5- 128Kbps，6- 192Kbps，7- 40Kbps，8- 48Kbps，9- 56Kbps，10- 80Kbps，11- 96Kbps，12- 112Kbps，13- 144Kbps，14- 160Kbps */
    uint8_t audioSamplingRate; /*!< 音频采样率：0- 默认，1- 16kHZ，2- 32kHZ，3- 48kHZ, 4- 44.1kHZ，5- 8kHZ */
    uint8_t volume;            /*!< 音量 [0,100] */
    uint8_t denoise;           /*!< 0-关闭 ，1开启 */
} UNIV_DEV_AUDIO_ENC_PARAM;

/***************** 图像参数 *****************/

/**
 * @brief 图像调节
 *
 */
typedef struct {
    uint8_t brightness; /*!< 亮度，取值范围[0,100] */
    uint8_t contrast;   /*!< 对比度，取值范围[0,100] */
    uint8_t saturation; /*!< 饱和度，取值范围[0,100] */
    uint8_t sharpness;  /*!< 锐度，取值范围[0,100] */
    uint8_t hue;        /*!< 色度，取值范围[0,100]，保留 */
} UNIV_DEV_CAMERA_VIDEOEFFECT;
/**
 * @brief 曝光
 *
 */
typedef struct {
    uint8_t mode;               /*!< 曝光：0-自动曝光(最大快门限制 最小快门限制 增益限制)，1-手动曝光(光圈 快门 增益)，2-光圈优先(光圈 最大快门限制 最小快门限制 增益限制)，3-快门优先(快门 增益限制) */
    uint8_t shutter;            /*!< 快门：0-1/25, 1-1/50, 2-1/75, 3-1/100, 4-1/125, 5-1/150, 6-1/175, 7-1/200, 8-1/225, 9-1/250, 10-1/300, 11-1/400, 12-1/500, 13-1/750, 14-1/1000, 15-1/2000, 16-1/4000, 17-1/10000, 18-1/100000 */
    uint8_t shutterMax;         /*!< 最大快门限制：同上 */
    uint8_t shutterMin;         /*!< 最小快门限制：同上 */
    uint8_t iris;               /*!< 光圈：[0,100] */
    uint8_t irisMax;            /*!< 光圈：[0,100] */
    uint8_t irisMin;            /*!< 光圈：[0,100] */
    uint8_t gain;               /*!< 增益：[0,100] */
    uint8_t gainLimit;          /*!< 增益限制：[0,100] */
    uint8_t lowLightLimitMode;  /*!< 低照度电子快门：0-关闭，1-开启 */
    uint8_t lowLightLimitLevel; /*!< 低照度电子快门：1-慢快门*2，2-慢快门*3，3-慢快门*4，4-慢快门*6，5-慢快门*8 */
} UNIV_DEV_CAMERA_EXPOSURE;
/**
 * @brief 白平衡
 *
 */
typedef struct {
    uint8_t mode;  /*!< 白平衡模式：0-手动，1-自动，2-户外，3-日光灯，4-钠灯，5-白炽灯，6-暖光灯，7-自然光 */
    uint8_t rGain; /*!< 手动模式 红色增益[0,100] */
    uint8_t gGain; /*!< 手动模式 绿色增益[0,100] */
    uint8_t bGain; /*!< 手动模式 蓝色增益[0,100] */
} UNIV_DEV_CAMERA_WHITEBALANCE;
/**
 * @brief 图像校正
 *
 */
typedef struct {
    uint8_t gamma;      /*!< 伽马：[1,3] */
    struct {            /*!< 坏点校正 */
        uint8_t enable; /*!< 开关 */
        uint8_t level;  /*!< 范围：[1,100] */
    } deadpixel;
} UNIV_DEV_CAMERA_GAMMACORRECT;

/**
 * @brief 背光补偿，与强光抑制、宽动态功能互斥，只能开启一个
 *
 */
typedef struct {
    uint8_t enable; /*!< 背光补偿：0-关闭，1-开启 */
    uint8_t area;   /*!< 区域：0-上，1-下，2-左，3-右，4-中，5-自定义 */
    RECT_ rect;     /*!< 自定义区域坐标：[0,1000] */
} UNIV_DEV_CAMERA_BACKLIGHT;
/**
 * @brief 强光抑制，与背光补偿、宽动态功能互斥，只能开启一个
 *
 */
typedef struct {
    uint8_t enable; /*!< 强光抑制：0-关闭，1-开启 */
    uint8_t level;  /*!< 等级：[0,100] */
} UNIV_DEV_CAMERA_HLC;
/**
 * @brief 宽动态，与背光补偿、强光抑制功能互斥，只能开启一个
 *
 */
typedef struct {
    uint8_t mode;  /*!< 宽动态：0-关闭，1-开启，2-自动 */
    uint8_t level; /*!< 等级：[1,10] */
} UNIV_DEV_CAMERA_WIDEDYNAMIC;
/**
 * @brief 光线调节
 *
 */
typedef struct {
    UNIV_DEV_CAMERA_BACKLIGHT backlight;
    UNIV_DEV_CAMERA_HLC hlc;
    UNIV_DEV_CAMERA_WIDEDYNAMIC wideDynamic;
    uint8_t lightLevel; /*!< 光线强度等级 [0,100] */
    uint8_t strobeMode; /*!< 工频闪模式：0-关闭，1-50hz，2-60hz */
} UNIV_DEV_CAMERA_LIGHTREGULATION;
/**
 * @brief 图像增强
 *
 */
typedef struct {
    struct {                   /*!< 数字降噪 */
        uint8_t enable;        /*!< 数字降噪：0-关闭，1-开启 */
        uint8_t mode;          /*!< 数字降噪：0-普通降噪，1-3D降噪 */
        uint8_t nomalLevel;    /*!< 普通降噪等级[0,100] */
        uint8_t spectralLevel; /*!< 空域降噪等级[0,100] */
        uint8_t temporalLevel; /*!< 时域降噪级别[0,100] */
    } denoise;
    struct {           /*!< 透雾 */
        uint8_t mode;  /*!< 透雾模式：0-关闭, 1-开启，2-自动，3-智能 */
        uint8_t level; /*!< 透雾等级：[1,100] */
    } dehaze;
    struct {           /*!< 图像防抖 */
        uint8_t mode;  /*!< 防抖模式：0-不支持，1-电子防抖，2-陀螺仪防抖，3-光学防抖 */
        uint8_t level; /*!< 防抖等级 */
    } imageStabilization;
    struct {            /*!< 热浪 */
        uint8_t enable; /*!< 热浪开关：0-关闭，1-开启 */
        uint8_t level;  /*!< 热浪等级：[1,100] */
    } heatWave;
} UNIV_DEV_CAMERA_IMAGEENHANCEMENT;
/**
 * @brief 聚焦
 *
 */
typedef struct {
    uint8_t mode;           /*!< 聚焦模式：0-自动，1-手动，2-半自动 */
    uint8_t initialize;     /*!< 镜头初始化：0-关闭，1-开启 */
    uint8_t sensitivity;    /*!< 灵敏度：0-低，1-中，2-高 */
    uint8_t minFocusLength; /*!< 最小聚焦距离：10cm，20cm，30cm，50cm，80cm，1.0m，1.5m，2.0m，2.5m，3.0m，5.0m，6.0m，7.0m，10.0m，20.0m，50.0m，infinity */
    uint16_t ratioLimit;    /*!< 倍率限制：33，66，132，264，396，528 */
    uint8_t ratioShow;      /*!< 倍率显示：0，1，2 */
    uint8_t alg;            /*!< 聚焦算法：0，1 */
} UNIV_DEV_CAMERA_FOCUS;
/**
 * @brief 镜像
 *
 */
typedef struct {
    uint8_t mode; /*!< 镜像模式：0-关闭，1-左右，2-上下，3-中心 */
} UNIV_DEV_CAMERA_MIRROR;
/**
 * @brief 日夜切换
 *
 */
typedef struct {
    uint8_t mode;             /*!< 日夜模式切换：0-白天，1-夜晚，2-自动，3-定时，4-报警，5光敏电阻 */
    uint8_t autoSensitivity;  /*!< 自动模式灵敏度 */
    uint8_t photoSensitivity; /*!< 光敏灵敏度 */
    uint8_t alarmActionType;  /*!< 报警触发状态：白天，夜晚 */
    TIMERANGE scheduleTime;   /*!< 时间范围内为白天 */
    struct {
        uint8_t enable;                /*!< 开关：0-关闭，1-开启 */
        uint8_t mode;                  /*!< 模式：0-自动，1-手动 */
        uint8_t manual_distance_level; /*!< 手动模式下 */
    } smartIR;
} UNIV_DEV_CAMERA_DAY_NIGHT_SWITCH;
/**
 * @brief 视频制式，PAL25帧，NTSC30帧
 *
 */
typedef struct {
    uint8_t mode;     /*!< 制式：0-PAL(50HZ)，1-NTSC(60HZ) */
    char capture[32]; /*!< 视频模式 */
} UNIV_DEV_CAMERA_VIDEO_STANDARD;
/**
 * @brief 本地输出，部分机芯可选
 *
 */
typedef struct {
    uint8_t mode; /*!< 本地输出：0-关闭，1-BNC，2-SDI,3-HDMI */
} UNIV_DEV_CAMERA_LOCAL_OUTPUT;
/**
 * @brief 旋转
 *
 */
typedef struct {
    uint8_t mode; /*!< 旋转：0-0，1-90，2-180，3-270 */
} UNIV_DEV_CAMERA_ROTATE;
/**
 * @brief 阴影校正
 *
 */
typedef struct {
    uint8_t mode;  /*!< 阴影校正：0-关闭，2-自动 */
    uint8_t level; /*!< 阴影校正强度：[0,100] */
} UNIV_DEV_CAMERA_SHADINGCORRECTION;

/**
 * @brief 图像参数基本配置
 *
 */
typedef struct {
    UNIV_DEV_CAMERA_VIDEOEFFECT videoEffect;           /*!< 图像参数 */
    UNIV_DEV_CAMERA_EXPOSURE exposure;                 /*!< 曝光参数 */
    UNIV_DEV_CAMERA_WHITEBALANCE whiteBalance;         /*!< 白平衡 */
    UNIV_DEV_CAMERA_GAMMACORRECT gammaCorrect;         /*!< gamma校正 */
    UNIV_DEV_CAMERA_LIGHTREGULATION lightRegulation;   /*!< 光照参数 */
    UNIV_DEV_CAMERA_IMAGEENHANCEMENT imageEnhancement; /*!< 图像增强 */
} UNIV_DEV_CAMERA_BASIC_PARAM;

/**
 * @brief 图像参数额外配置，定时模式下不可配置
 *
 */
typedef struct {
    UNIV_DEV_CAMERA_FOCUS focus;                         /*!< 聚焦参数 */
    UNIV_DEV_CAMERA_MIRROR mirror;                       /*!< 镜像参数 */
    UNIV_DEV_CAMERA_DAY_NIGHT_SWITCH dayNightSwitch;     /*!< 日夜切换 */
    UNIV_DEV_CAMERA_VIDEO_STANDARD videoStandard;        /*!< 图像制式 */
    UNIV_DEV_CAMERA_LOCAL_OUTPUT localOutput;            /*!< 本地输出 */
    UNIV_DEV_CAMERA_ROTATE rotate;                       /*!< 图像旋转 */
    UNIV_DEV_CAMERA_SHADINGCORRECTION shadingCorrection; /*!< 阴影校正 */
} UNIV_DEV_CAMERA_EXTRA_PARAM;

/**
 * @brief 图像参数配置
 *
 */
typedef struct {
    // Get时，-1表示获取当前生效场景，其他值获取对应场景
    uint8_t scene; /*!< 图像场景：0-自动，1-定时，2-户外，3-夜晚，4-道路 */
    union {
        /*!< 非定时模式使用 */
        struct {
            UNIV_DEV_CAMERA_BASIC_PARAM basic;
            UNIV_DEV_CAMERA_EXTRA_PARAM extra;
        } fullParam;
        /*!< 定时模式使用 */
        struct {
            TIMERANGE scheduleTime; /*!< 定时切换时间，时间段内为白天，时间段外为夜晚 */
            UNIV_DEV_CAMERA_BASIC_PARAM day;
            UNIV_DEV_CAMERA_BASIC_PARAM night;
        } timingParam;
    };
} UNIV_DEV_CAMERA_PARAM;

/***************** OSD参数 *****************/

/**
 * @brief OSD数据
 *
 */
typedef struct {
    char data[32];  /*!< 内容：utf-8编码 */
    uint8_t enable; /*!< 启用：0-关闭，1-开启 */
    uint8_t align;  /*!< 对齐方式 (不生效) */
    COORDINATE pos;
} UNIV_DEV_OSD_DATA;
/**
 * @brief OSD配置信息
 *
 */
typedef struct {
    uint8_t customColorEnable; /*!< 自定义颜色开启：0-关闭，1-开启 */
    uint8_t customColorR;      /*!< 自定义颜色 */
    uint8_t customColorG;
    uint8_t customColorB;
    uint8_t osdAttrib;              /*!< OSD属性：0-透明闪烁，1-透明不闪烁，2-不透明闪烁，3-不透明不闪烁 */
    uint8_t fontSize;               /*!< 字体大小：0-16*16，1-32*32，2-48*48，3-64*64，4-自适应，5-96*96 */
    uint8_t timeFormat;             /*!< 时间格式：0-24小时，1-12小时 */
    uint8_t dateFormat;             /*!< 日期格式：0-YYYY-MM-DD，1-MM-DD-YYYY，2 DD-MM-YYYY，3-YYYY年MM月DD日，4-MM月DD日YYYY年，5-DD日MM月YYYY年，6-YYYY/MM/DD，7-MM/DD/YYYY，8-DD/MM/YYYY */
    uint8_t enableTime;             /*!< 时间显示：0-关闭，1-开启 */
    uint8_t enableWeek;             /*!< 星期显示：0-关闭，1-开启 */
    uint8_t enableSnap;             /*!< 抓图OSD开关：0-关闭，1-开启 */
    COORDINATE timePosition;        /*!< 时间OSD坐标 */
    UNIV_DEV_OSD_DATA titleOSD;     /*!< 通道标题 */
    UNIV_DEV_OSD_DATA customOSD[8]; /*!< 自定义OSD */
    /**** 温漂或温度OSD开关 具体功能是否存在，参照相机网页 ****/
    uint8_t temperatureDrift;       /*!< 温漂开关：0-关闭，1-开启 */
    uint8_t cpuTemperatureDisp;     /*!< CPU温度开关：0-关闭，1-开启 */
    uint8_t fovFlDisp;              /*!< 镜头信息开关：0-关闭，1-开启 */
    uint8_t temperatureDisp;        /*!< 温度显示开关：0-关闭，1-开启 */
    uint8_t thermalTemperatureDisp; /*!< 热像温度开关：0-关闭，1-开启 */
} UNIV_DEV_OSD_PARAM;

/***************** 智能化信息配置参数 *****************/
/**
 * @brief 检测区域
 *
 */
typedef struct {
    uint8_t enable;
    POLYGON polygon;
} DETECTREGION;
/**
 * @brief 全结构化配置参数
 *
 */
typedef struct {
    uint8_t enable; /*!< 结构化开启：0-关闭，1-开启 */
    struct {
        uint8_t detectEnable;      /*!< 检测开关：0-关闭，1-开启 */
        uint8_t statisticalEnable; /*!< 统计开关：0-关闭，1-开启 */
        uint8_t osdEnable;         /*!< OSD信息叠加开关：0-关闭，1-开启 */

        uint8_t image1Quality;     /*!< 小图1图像质量：人体、机动车、非机动车：0-最好，1-较好，2-一般 */
        uint8_t image2Quality;     /*!< 小图2图像质量：人脸、车牌、无：0-最好，1-较好，2-一般 */
        uint8_t backgroundQuality; /*!< 背景大图质量：0-最好，1-较好，2-一般 */
        uint8_t confidenceLevel;   /*!< 置信度：[1,10] */
    } body, motor, nonmotor;

    TIMERANGE alarmSchedule[7][8]; /*!< 布控时间，每周7天，每天最多可配置8个时间段 */
    DETECTREGION detectRegion[4];  /*!< 监控区域，最多4个 */

    struct {
        uint8_t triggerAlarmOut; /*!< 联动报警输出 */
        uint8_t triggerCenter;   /*!< 联动上传中心 */
        uint8_t triggerEmail;    /*!< 联动电子邮件 */
        uint8_t triggerFTP;      /*!< 联动上传ftp */
        uint8_t triggerRec;      /*!< 联动录像 */
        uint8_t triggerVoiceTip; /*!< 联动提示音 */
    } triggerList;

} UNIV_DEV_VIDEO_STRUCTURATION_DETECT_PARAM;
/**
 * @brief 全结构化OSD配置参数
 *
 */
typedef struct {
    /*!< 推图配置 */
    uint8_t webPush; /*!< 推图总开关 */
    struct {
        uint8_t imageUpload;      /*!< 上传小图：0-关闭，1-开启 */
        uint8_t backgroundUpload; /*!< 上传背景大图：0-关闭，1-开启 */
        uint8_t smartInsertPic;   /*!< 智能插图：0-关闭，1-开启 */
    } bodyUpload, motorUpload, nonMotorUpload;

    uint8_t timeOSDEnable; /*!< 图片叠加时间戳开关：0-最好，1-较好，2-一般 */
    uint8_t siteOSDEnable; /*!< 图片叠加地点开关：0-最好，1-较好，2-一般 */
    char siteOSDInfo[32];  /*!< 图片叠加地点内容 */

    /*!< 图片信息叠加 */
    uint8_t bodyAttr[8];        /*!< 属性：性别、上衣颜色、上衣类型、下衣颜色、下衣类型、背包、帽子、安全帽 */
    uint8_t motorAttr[12];      /*!< 属性：车牌、车身颜色、车辆类型、车标、车牌颜色、车系、遮阳板、安全的、抽烟状态、打电话状态、车内饰品、年检标志 */
    uint8_t nonMotorAttr[5];    /*!< 属性：车辆类型、车身颜色、骑车人数、上衣颜色、上衣类型 */
    uint8_t detectAreaOverlay;  /*!< 叠加检测框开关：0-最好，1-较好，2-一般 */
    uint8_t monitorInfoOverlay; /*!< 叠加监测点信息开关：0-最好，1-较好，2-一般 */
    uint8_t imageOverlay[3];    /*!< 监测点信息选择：0关闭，1设备编号，2监测点字符信息，3抓拍时间 */
    char deviceNO[32];          /*!< 设备编号 */
    char monitorInfo[32];       /*!< 监测点字符信息 */
} UNIV_DEV_VIDEO_STRUCTURATION_OSD_PARAM;

/**
 * @brief 智能人脸配置参数
 *
 */
typedef struct {
    uint8_t enable;      /*!< 是否启动智能人脸抓拍：0-关闭，1-开启 */
    uint8_t snapType;    /*!< 抓拍目标：0-人脸，4-一寸照、5-自定义 */
    float faceExpansion; /*!< 人脸抠图规格，自定义模式使用：[1.1,4.0] */
    struct {
        uint8_t mode; /*!< 推图策略：0-快速，1-最优 */
        /*!< 快速模式 */
        struct {
            uint8_t fastMode;  /*!< 抓拍次数：0-有限次，1-无限次 */
            uint8_t snapCount; /*!< 有限次数下抓拍张数：[1,100] */
            uint16_t snapTime; /*!< 推图间隔时间：[0,600] */
        } fast;
        /*!< 最优模式 */
        struct {
            uint8_t snapCount; /*!< 抓拍张数：[1,3] */
            uint16_t snapTime; /*!< 推图过滤时间：[1,10] */
        } best;
    } strategy;
    /*!< 人脸曝光 */
    struct {
        uint8_t enable;          /*!< 人脸曝光开关：0-关闭，1-开启 */
        uint8_t referBrightness; /*!< 参考亮度：[0,100] */
        uint8_t minDuration;     /*!< 最短持续时间：[0,60]分钟 */
    } faceExposure;
    /*!< 高级设置 */
    struct {
        uint8_t snapMode;        /*!< 抓拍模式：2-自定义 */
        uint8_t confidenceLevel; /*!< 置信度：[1,10] */
        struct {
            uint8_t face3dPose_yaw;   /*!< 左右角度：[0,90] */
            uint8_t face3dPose_pitch; /*!< 上下角度：[0,90] */
            uint8_t face3dPose_roll;  /*!< 旋转角度：[0,180] */
            uint8_t blurness;         /*!< 清晰度：[0,100] */
        } custom;
    } advancedProperty;
} UNIV_DEV_FACE_SNAP_PARAM;

/**
 * @brief 智能人脸配置参数
 *
 */
typedef struct {
    uint8_t imageQuality;                      /*!< 图片质量：0-最好，1-较好，2-一般 */
    uint8_t backgroundQuality;                 /*!< 背景图片质量：0-最好，1-较好，2-一般 */
    UNIV_DEV_VIDEO_RESOLUTION imageResolution; /*!< 图片分辨率：不可修改 */
    uint8_t imageUpload;                       /*!< 是否上传小图：0-关闭，1-开启 */
    uint8_t backgroundUpload;                  /*!< 是否上传背景图片：0-关闭，1-开启 */
    uint8_t webPush;                           /*!< 推图开关：0-关闭，1-开启 */
    uint8_t smartInsertPic;                    /*!< 智能插图：0-关闭，1-开启 */
    uint8_t detectAreaOverlay;                 /*!< 叠加检测框开关：0-最好，1-较好，2-一般 */
    uint8_t monitorInfoOverlay;                /*!< 叠加监测点信息开关：0-最好，1-较好，2-一般 */
    uint8_t imageOverlay[3];                   /*!< 监测点信息选择：0关闭，1设备编号，2监测点字符信息，3抓拍时间 */
    char deviceNO[32];                         /*!< 设备编号 */
    char monitorInfo[32];                      /*!< 监测点字符信息 */
} UNIV_DEV_FACE_SNAP_OSD_PARAM;

/***************** 云台配置参数 *****************/

/**
 * @brief 云台预置点信息
 *
 */
typedef struct {
    struct {
        uint8_t preset; /*!< 预置点序号[1,255] */
        char name[21];  /*!< 预置点名称 */
    } preset[255];
} UNIV_DEV_PTZ_TRACK_PARAM;

/**
 * @brief 云台巡航路径
 *
 */
typedef struct {
    uint8_t index; /*!< 巡航路径：[1,8]，设置对应巡航路径时使用 */
    struct {
        uint8_t preset; /*!< 预置点：[1,255] */
        uint8_t speed;  /*!< 速度：[1,40] */
        uint8_t time;   /*!< 停留时间：[10,120] */
    } cruise[8][32];
} UNIV_DEV_PTZ_CRUISE_PARAM;

/**
 * @brief PTZ坐标值
 *
 *  实际显示的PT值是获取到的数值的百分之一
 *  如获取的水平参数P的值是17500，实际显示的P值为175度
 *  获取到的垂直参数T的值是7890，实际显示的T值为78.9度
 *  Zoom/Focus的值为索尼坐标值
 */
typedef struct {
    uint32_t action;   /*!< 仅用于设置：1-定位PTZ参数，2-定位PT参数，3-定位P参数，4-定位T参数，5-定位ZF参数，6-定位Z参数，7-定位F参数 */
    uint32_t panPos;   /*!< 水平参数 */
    uint32_t tiltPos;  /*!< 垂直参数 */
    uint32_t zoomPos;  /*!< 变倍参数 */
    uint32_t focusPos; /*!< 聚焦参数 */
} UNIV_DEV_PTZ_POS_PARAM;

/**
 * @brief 云台PTZ范围界限值
 *
 */
typedef struct {
    uint32_t panPosMin;   /*!< 水平参数min */
    uint32_t panPosMax;   /*!< 水平参数max */
    uint32_t tiltPosMin;  /*!< 垂直参数min */
    uint32_t tiltPosMax;  /*!< 垂直参数max */
    uint32_t zoomPosMin;  /*!< 变倍参数min */
    uint32_t zoomPosMax;  /*!< 变倍参数max */
    uint32_t focusPosMin; /*!< 聚焦参数min */
    uint32_t focusPosMax; /*!< 聚焦参数max */
} UNIV_DEV_PTZ_SCOPE_PARAM;

/***************** 抓图配置参数 *****************/

/**
 * @brief 连续抓图配置
 *
 */
typedef struct {
    uint8_t fps;                          /*!< 帧率 */
    UNIV_DEV_VIDEO_RESOLUTION resolution; /*!< 图片分辨率 */
    uint8_t quality;                      /*!< 图片质量：[1,100] */
    uint8_t format;                       /*!< 图片格式：0-jpeg */
} UNIV_DEV_SNAP_PARAM;

/***************** GB28181配置参数 *****************/

/**
 * @brief GB28181配置
 *
 */
typedef struct {
    uint8_t enable;             /*!< 开关 */
    uint8_t version;            /*!< 0-GB2011；1-GB2016 */
    uint16_t localPort;         /*!< 本地SIP端口 */
    uint16_t serverPort;        /*!< 服务器端口 */
    char serverID[64];          /*!< 服务器ID */
    char serverDomain[64];      /*!< 服务器域 */
    char serverIP[64];          /*!< 服务器IP */
    char username[64];          /*!< 用户名 */
    char password[32];          /*!< 密码 */
    uint8_t stream;             /*!< 码流：0-主码流，1-子码流 */
    uint32_t expire;            /*!< 注册有效期（秒） */
    uint8_t regStatus;          /*!< 注册状态：0-离线，1-在线 */
    uint8_t heartbeat;          /*!< 心跳周期（秒） */
    uint8_t maxTimeout;         /*!< 最大心跳超时次数 */
    uint8_t transProtocol;      /*!< 传输协议：0-UDP，1-TCP */
    uint32_t regInterval;       /*!< 注册间隔 */
    char videoChannelID[2][32]; /*!< 视频通道ID */
    char audioChannelID[2][32]; /*!< 语音通道ID */
    char alarmChannelID[2][32]; /*!< 报警通道ID */
} UNIV_DEV_GB28181_PARAM;

/***************** 串口配置参数 *****************/

/**
 * @brief 232串口配置
 *
 */
typedef struct {
    uint8_t baudRate; /*!< 波特率：0－50，1－75，2－110，3－150，4－300，5－600，6－1200，7－2400，8－4800，9－9600，10－19200，11－38400，12－57600，13－76800，14－115200 */
    uint8_t dataBit;  /*!< 数据位：0－5位，1－6位，2－7位，3－8位 */
    uint8_t stopBit;  /*!< 停止位：0－1位，1－2位 */
    uint8_t parity;   /*!< 校验位：0－无校验，1－奇校验，2－偶校验 */
    uint8_t flowCtrl; /*!< 流控：0－无，1－软流控 */
    uint8_t workMode; /*!< 工作模式：1-控制台 2-透明通道 */
} UNIV_DEV_RS232_PARAM;

/**
 * @brief 当前日夜状态
 *
 */
typedef struct {
    uint8_t icrStatus; /*!< ICR状态：0-白天（彩色） 1-夜晚（黑白） */
    uint8_t dayNight;  /*!< 日夜状态：0-白天（彩色） 1-夜晚（黑白） */
} UNIV_DEV_PTZ_DAYNIGHT_STATUS_PARAM;

/**
 * @brief GPS信息
 *
 */
typedef struct {
    double longitude; // 经度，范围【±180.000000】
    double latitude;  // 纬度，范围【±90.000000】
    double altitude;  // 海拔高度，范围【-9999.9 ~ 9999.9】
    double speed;     // 速度单位 km/h，范围【0 ~ 999.9】
    double direction; // 方向为以正北为基准，范围【0 ~ 359.9】
    double angle;     // 角度为以磁场正北为基准，磁偏角，【0~359.9】
    uint32_t star;    // 星数，范围【0~99】
    uint32_t signal;  // 信号强度，范围【0-99】
    uint64_t time;    // UTC时间
} UNIV_DEV_PTZ_GEO_PARAM;

/**
 * @brief 建立透明通道 (暂时不需要设置工作模式，直接发送数据)
 *        1) 232透明通道需要先通过UNIV_DEV_SetConfig设置RS232的工作模式，485不需要;
 *        2) 先调用UNIV_DEV_SerialSend接口发送232/485数据后，后续才能收到对应通道的数据通知
 *
 * @param userID 用户ID
 * @param channel 通道号：0
 * @param pfnDataCallBack 回调函数指针， dataType 为channel，1- 232 串口，2- 485 串口
 * @param pSerialHandle 成功返回句柄
 * @return UNIV_ERROR_CODE
 */
UNIV_API int32_t UNIV_CALL UNIV_DEV_SerialStart(uint64_t userID, uint8_t channel, UNIV_RealDataCallBack pfnDataCallBack, uint64_t* pSerialHandle);

/**
 * @brief 透明通道发送数据
 *
 * @param serialHandle 透明通道句柄
 * @param channel 通道号：1- 232 串口，2- 485 串口
 * @param pData 发送内容
 * @param dataSize 发送内容数据长度, 不得超过1024
 * @return UNIV_ERROR_CODE
 */
UNIV_API int32_t UNIV_CALL UNIV_DEV_SerialSend(uint64_t serialHandle, uint8_t channel, void* pData, uint32_t dataSize);

/**
 * @brief 关闭透明通道
 *
 * @param serialHandle 透明通道句柄
 * @return UNIV_ERROR_CODE
 */
UNIV_API int32_t UNIV_CALL UNIV_DEV_SerialStop(uint64_t serialHandle);

/***************** 配置命令集 *****************/
/**
 * @brief 配置参数
 *
 */
typedef enum {
    UNIV_CFG_VIDEO_ENCODE,               /*!< 视频编码配置      | Get/Set       | UNIV_DEV_VIDEO_ENC_PARAM                  | stream可作入参 */
    UNIV_CFG_AUDIO_ENCODE,               /*!< 音频编码配置      | Get/Set       | UNIV_DEV_AUDIO_ENC_PARAM */
    UNIV_CFG_CAMERA,                     /*!< 图像参数          | Get/Set/Reset | UNIV_DEV_CAMERA_PARAM                     | scene 可做入参 */
    UNIV_CFG_OSD,                        /*!< OSD叠加           | Get/Set       | UNIV_DEV_OSD_PARAM */
    UNIV_CFG_VIDEO_STRUCTURATION_DETECT, /*!< 全结构化          | Get/Set/Reset | UNIV_DEV_VIDEO_STRUCTURATION_DETECT_PARAM | reset也会重置全结构化图片OSD配置 */
    UNIV_CFG_VIDEO_STRUCTURATION_OSD,    /*!< 全结构化图片OSD   | Get/Set       | UNIV_DEV_VIDEO_STRUCTURATION_OSD_PARAM */
    UNIV_CFG_FACE_SNAP,                  /*!< 人脸抓拍          | Get/Set       | UNIV_DEV_FACE_SNAP_PARAM */
    UNIV_CFG_FACE_SNAP_OSD,              /*!< 人脸OSD           | Get/Set       | UNIV_DEV_FACE_SNAP_OSD_PARAM */
    UNIV_CFG_PTZ_PRESET,                 /*!< 云台预置位        | Get           | UNIV_DEV_PTZ_TRACK_PARAM */
    UNIV_CFG_PTZ_CRUISE,                 /*!< 云台巡航          | Get/Set       | UNIV_DEV_PTZ_CRUISE_PARAM */
    UNIV_CFG_PTZ_3DPOSE,                 /*!< 3D定位            | Set           | RECT | 屏幕左上角[0,0] 右下角 [1000,1000] */
    UNIV_CFG_PTZ_AREAFOCUS,              /*!< 区域聚焦          | Set           | RECT | 屏幕左上角[0,0] 右下角 [1000,1000] */
    UNIV_CFG_PTZ_AREAEXPOSURE,           /*!< 区域曝光 	        | Set           | RECT | 屏幕左上角[0,0] 右下角 [1000,1000] */
    UNIV_CFG_SNAP,                       /*!< 抓图配置 	        | Get/Set       | UNIV_DEV_SNAP_PARAM */
    UNIV_CFG_SNAP_OSD,                   /*!< 抓图OSD配置	    | Get/Set       | UNIV_DEV_OSD_PARAM | 定制接口，不要使用 */
    UNIV_CFG_GB28181,                    /*!< 国标配置	        | Get/Set       | UNIV_DEV_GB28181_PARAM */
    UNIV_CFG_PTZ_POS,                    /*!< PTZ位置	        | Get/Set       | UNIV_DEV_PTZ_POS_PARAM */
    UNIV_CFG_PTZ_SCOPE,                  /*!< PTZ位置范围       | Get           | UNIV_DEV_PTZ_SCOPE_PARAM */
    UNIV_CFG_RS232,                      /*!< 232串口配置       | Get/Set       | UNIV_DEV_RS232_PARAM */
    UNIV_CFG_NTP,                        /*!< NTP时间配置       | Get/Set       | UNIV_DEV_NTP_PARAM */
    UNIV_CFG_TIME,                       /*!< 系统时间配置      | Get/Set       | UNIV_DEV_TIME_PARAM */
    UNIV_CFG_DEVICE_INFO,                /*!< 系统信息          | Get           | UNIV_DEV_DEVICE_INFO_PARAM */
    UNIV_CFG_PTZ_ZOOM_RATION,            /*!< Zoom倍率值        | Get/Set       | uint32_t */
    UNIV_CFG_PTZ_DAYNIGHT_STATUS,        /*!< 日夜切换状态       | Get           | UNIV_DEV_PTZ_DAYNIGHT_STATUS_PARAM */
    UNIV_CFG_PTZ_GEO,                    /*!< GPS信息          | Get           | UNIV_DEV_PTZ_GEO_PARAM */
    UNIV_CFG_INVALID = -1,
} UNIV_CFG_COMMAND;

/**
 * @brief 获取设备配置
 *
 * @param userID 用户ID
 * @param command UNIV_CFG_COMMAND
 * @param pOutBuffer 数据缓存，成功后返回内容为各配置命令对应的结构体
 * @param outBufferSize 数据缓存长度
 * @return UNIV_ERROR_CODE
 */
UNIV_API int32_t UNIV_CALL UNIV_DEV_GetConfig(uint64_t userID, UNIV_CFG_COMMAND command, void* pOutBuffer, uint32_t outBufferSize);

/**
 * @brief 设置设备配置
 *
 * @param userID 用户ID
 * @param command UNIV_CFG_COMMAND
 * @param pInBuffer 配置内容，内容为各配置命令对应的结构体
 * @param inBufferSize 数据长度
 * @return UNIV_ERROR_CODE
 */
UNIV_API int32_t UNIV_CALL UNIV_DEV_SetConfig(uint64_t userID, UNIV_CFG_COMMAND command, void* pInBuffer, uint32_t inBufferSize);

/**
 * @brief 恢复默认配置
 *
 * @param userID 用户ID
 * @param command UNIV_CFG_COMMAND
 * @return UNIV_ERROR_CODE
 */
UNIV_API int32_t UNIV_CALL UNIV_DEV_ResetConfig(uint64_t userID, UNIV_CFG_COMMAND command);

/***************** 智能化信息 *****************/

/**
 * @brief 基本图片信息，背景大图
 *
 */
typedef struct {
    void* data;         /*!< 图片数据，jpeg格式 */
    uint32_t dataSize;  /*!< 图片数据长度 */
    RECT_ rect;         /*!< 图片在背景大图的坐标 */
    uint64_t pts;       /*!< 算法传上来的值，可以关联大小图 */
    uint64_t timestamp; /*!< 机芯收到图片的时间，微秒 */
    uint32_t trackId;   /*!< 图片ID */
    float quality;      /*!< 图片质量 */
    char address[64];   /*!< 地址信息，通过全结构化OSD配置 */
} BaseObjectInfo, BackGroundObjectInfo;
/**
 * @brief 人脸信息
 *
 */
typedef struct {
    BaseObjectInfo image;

    uint8_t sex;        /*!< 性别：0-未知，1-男，2-女 */
    uint8_t age;        /*!< 年龄 */
    uint8_t skin_color; /*!< 肤色：0-未知，1-黄皮肤，2-白皮肤，3-黑皮肤，4-深色皮肤 */
    uint8_t hat;        /*!< 帽子：0-未知，1-带帽子，2-不戴帽子 */
    uint8_t hair;       /*!< 头发：0-未知，1-光头，2-很少头发，3-短头发，4-长头发，5-其他发型 */
    uint8_t glasses;    /*!< 眼镜：0-未知，1-带眼镜，2-不戴眼镜 */
    uint8_t respirator; /*!< 口罩：0-未知，1-带口罩，2-不戴口罩 */
    uint8_t beard;      /*!< 胡须：0-未知，1-没有胡子或者胡子很不明显，2-小胡子，3-腮须，4-其他类型 */
} FaceObjectInfo;
/**
 * @brief 人体信息
 *
 */
typedef struct {
    BaseObjectInfo image;

    int8_t sex;                 /*!< 性别：0-未知，1-男性，2-女性 */
    int8_t coat_color;          /*!< 上衣颜色：0-未知，1-黑色，2-白色，3-灰色，4-红色，5-橙色，6-黄色，7-绿色，8-深蓝色，9-浅蓝色，10-紫色，11-粉红色，12-棕色，13-彩色 */
    int8_t coat_type;           /*!< 上衣类型：0-未知，1-长袖，2-短袖 */
    int8_t under_clothes_color; /*!< 下衣颜色：0-未知，1-黑色，2-白色，3-灰色，4-红色，6-橙色，6-黄色，7-绿色，8-深蓝色，9-浅蓝色，10-紫色，11-粉红色，12-棕色，13-彩色 */
    int8_t under_clothes_type;  /*!< 下衣类型：0-未知，1-长裤，2-短裤 */
    int8_t backpack;            /*!< 背包：0-未知，1-背包，2-不背包 */
    int8_t helmet;              /*!< 安全帽：0-知，1-带帽子，2-不戴帽子 */
    int8_t hat_type;            /*!< 帽子类型：0-未知，1-安全帽，2-厨师帽，3-学生帽，4-头盔，5-小白帽，6-头巾 */
} BodyObjectInfo;
/**
 * @brief 车牌信息
 *
 */
typedef struct {
    BaseObjectInfo image;

    char plateNum[32];  /*!< 车牌内容 */
    int8_t plate_count; /*!< 车牌位数 */
    int8_t plate_color; /*!< 车牌颜色：0-未知，1-蓝色，2-黄色，3-黑色，4-白色，5-绿色，6-小型新能源，7-大型新能源 */
} PlateObjectInfo;
/**
 * @brief 机动车信息
 *
 */
typedef struct {
    BaseObjectInfo image;

    int8_t plate_color; /*!< 车牌颜色：0-未知，1-蓝色，2-绿色，3-白色，4-黑色 */
    char plateNum[32];  /*!< 车牌内容 */
    int8_t plate_count; /*!< 车牌位数 */
    int8_t motor_color; /*!< 车身颜色：0-未知，1-黑色，2-白色，3-银灰，4-棕色，5-红色，6-蓝色，7-黄色，8-绿色，9花色 */
    int8_t motor_type;  /*!< 车辆类型：0-未知，1-小轿车，2-SUV，3-面包车，4-中巴车和大巴车，5-皮卡车，6-卡车，7-其他 */

    int8_t motor_series;      /*!< 车系：0-未知，1-德系，2-日系，3-美系，4-韩系，5-国产，6-法系 */
    int8_t motor_visor;       /*!< 遮阳板：0-未知，1-有，2-无 */
    int8_t seat_belt;         /*!< 安全带：0-未知，1-有，2-无 */
    int8_t smoking_status;    /*!< 抽烟状态：0-未知，1-有，2-无 */
    int8_t phone_status;      /*!< 打电话状态：0-未知，1-有，2-无 */
    int8_t car_accessories;   /*!< 车内饰品：0-未知，1-有，2-无 */
    int8_t yearly_check_mark; /*!< 年检标志：0-未知，1-有，2-无 */

    char brandname[256]; /*!< 品牌(主品牌，年款，子品牌) 日产_奇骏_17/19年款 */
} MotorObjectInfo;
/**
 * @brief 非机动车信息
 *
 */
typedef struct {
    BaseObjectInfo image;

    int8_t noMotor_type;   /*!< 车辆类型：0-未知，1-自行车，2-电动车，3-摩托车，4-三轮车，5-其他 */
    int8_t noMotor_color;  /*!< 车身颜色：0-未知，1-红色，2-橙色，3-黄色，4-绿色，5-青色，6-蓝色，7-紫色 */
    int8_t cycling_number; /*!< 骑车人数：0-未知 */
    int8_t coat_color;     /*!< 上衣颜色：0-未知，1-红色，2-橙色，3-黄色，4-绿色，5-青色，6-蓝色，7-紫色 */
    int8_t coat_type;      /*!< 上衣类型：0-未知，1-短袖，2-长袖，3-裙子 */
} NonMotorObjectInfo;
/**
 * @brief 智能识别检测结果信息，无额外的识别信息
 *
 */
typedef struct {
    BaseObjectInfo image;
} ShipObjectInfo, PlaneObjectInfo, DroneObjectInfo, BirdObjectInfo, PersonObjectInfo, SmokeObjectInfo, FireObjectInfo;

/**
 * @brief 智能数据类型
 *
 */
typedef enum {
    SMART_DATA_INVALID = -1,
    // 结构化智能
    SMART_DATA_BACKGROUND, /*!< 背景大图 */
    SMART_DATA_FACE,       /*!< 人脸 */
    SMART_DATA_BODY,       /*!< 人体 */
    SMART_DATA_MOTOR,      /*!< 机动车 */
    SMART_DATA_NONMOTOR,   /*!< 非机动车 */
    SMART_DATA_PLATE,      /*!< 车牌 */
    // 无额外的智能属性
    SMART_DATA_SHIP,   /*!< 船只 */
    SMART_DATA_PLANE,  /*!< 飞机 */
    SMART_DATA_DRONE,  /*!< 无人机 */
    SMART_DATA_BIRD,   /*!< 鸟 */
    SMART_DATA_PERSON, /*!< 人形 */
    SMART_DATA_SMOKE,  /*!< 烟 */
    SMART_DATA_FIRE,   /*!< 火 */
} SMART_DATA_TYPE;

/**
 * @brief 监听智能信息，全结构化或人脸抓拍
 *
 * @param userID 用户ID
 * @param channel 通道，取值0
 * @param pfnDataCallBack @dataType：SMART_DATA_TYPE
 * @param pEventHandle 成功返回句柄
 * @return UNIV_ERROR_CODE
 */
UNIV_API int32_t UNIV_CALL UNIV_DEV_SubSmartEvents(uint64_t userID, uint8_t channel, UNIV_RealDataCallBack pfnDataCallBack, uint64_t* pEventHandle);

/**
 * @brief 取消订阅智能信息
 *
 * @param eventHandle 智能信息句柄
 * @return UNIV_ERROR_CODE
 */
UNIV_API int32_t UNIV_CALL UNIV_DEV_UnSubSmartEvents(uint64_t eventHandle);

/**
 * @brief 事件内容，包括文本信息和对应的图片，智能信息可能有2张图片，小图和背景图
 *
 */
typedef struct {
    struct {
        uint8_t* ptr;
        uint32_t len;
    } info, // 事件信息，json文本
      img0, // 事件图片
      img1; // 事件图片
} EventInfo;

/**
 * @brief 监听事件信息，包括报警信息和智能识别信息
 *
 * @param userID 用户ID
 * @param channel 通道，取值0
 * @param pfnDataCallBack
 * @param pEventHandle 成功返回句柄
 * @return UNIV_ERROR_CODE
 */
UNIV_API int32_t UNIV_CALL UNIV_DEV_SubEvents(uint64_t userID, uint8_t channel, UNIV_RealDataCallBack pfnDataCallBack, uint64_t* pEventHandle);

/**
 * @brief 取消订阅智能信息
 *
 * @param eventHandle 智能信息句柄
 * @return UNIV_ERROR_CODE
 */
UNIV_API int32_t UNIV_CALL UNIV_DEV_UnSubEvents(uint64_t eventHandle);

/***************** 控制命令集 *****************/

/**
 * @brief 产生关键帧
 *
 */
typedef struct {
    uint8_t channel; /*!< 通道号 */
    uint8_t stream;  /*!< 码流类型  STREAM_TYPE */
} UNIV_CTL_PARAM_I_FRAME;

/**
 * @brief 控制命令
 *
 */
typedef enum {
    UNIV_CTL_INVALID = -1,
    UNIV_CTL_KEY_FRAME,        /*!< 产生关键帧 | UNIV_CTL_PARAM_I_FRAME */
    UNIV_CTL_GET_FOCUS_STATUS, /*!< 查询聚焦状态 | int32_t */
} UNIV_CTL_COMMAND;

/**
 * @brief 向设备发送控制命令
 *
 * @param userID 用户ID
 * @param command 控制命令
 * @param pBuffer 参数结构体指针
 * @param bufferSize 参数结构体大小
 * @return UNIV_ERROR_CODE
 */
UNIV_API int32_t UNIV_CALL UNIV_DEV_RemoteControl(uint64_t userID, UNIV_CTL_COMMAND command, void* pBuffer, uint32_t bufferSize);

#pragma pack(pop)

#undef UNIV_API
#undef UNIV_CALL
#undef UNIV_CALLBACK

#ifdef __cplusplus
}
#endif
