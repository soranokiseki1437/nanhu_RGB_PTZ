#define _CRT_SECURE_NO_WARNINGS

// ================== 实时跟踪主程序 ==================
// 完全基于main_1.cpp架构
// 使用已修复的dsst_tracker.cpp算法

#include <stdio.h>
#include <unistd.h>
#include <vector>
#include <complex>

#include "opencv2/opencv.hpp"
#include "opencv2/core.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/imgproc.hpp"
#include "../gz0084FuncLib.h"  // 硬件接口库
#include "dsst_tracker.h"

using namespace cv;
using namespace std;

// ================== 全局变量 ==================
cv::Mat show_image(1080, 1920, CV_8UC3);      // 可见光显示缓冲区(1920x1080)
cv::Mat show_image_ir(512, 640, CV_8UC3);     // 红外显示缓冲区(640x512)

// 跟踪器实例（使用修复后的dsst_tracker）
DSSTTracker tracker;

// ROI选择状态
cv::Rect selected_roi;
bool has_selected_roi = false;
bool need_init_tracker = false;

void show_osd(cv::Mat &show_image)
{
    int centre_x = show_image.cols / 2;
    int centre_y = show_image.rows / 2;
    float _scale = 0.05;
    float _centre_scale = 0.01;
    double fontScale = 1.2;
    int thickness = 2;
    int fontFace = cv::FONT_HERSHEY_TRIPLEX;
    int cross_half_len = show_image.cols * _scale;
    int cross_centre_len = show_image.cols * _centre_scale;
    int x_start = 25;
    int y_start = 50;
    int step_y = 50;
    cv::Scalar text_color_1 = cv::Scalar(0, 255, 0);
    cv::Scalar text_color_2 = cv::Scalar(0, 0, 255);
    cv::Scalar text_color_3 = cv::Scalar(255, 0, 0);
    if (show_image.cols == 640) {
        fontScale = 0.6;
        thickness = 1;
        x_start = 5;
        y_start = 20;
        step_y = 30;
        text_color_1 = cv::Scalar(255, 255, 255);
        text_color_2 = cv::Scalar(255, 255, 255);
        text_color_3 = cv::Scalar(255, 255, 255);
    }
        
    cv::line(show_image, cv::Point(centre_x - cross_half_len, centre_y), cv::Point(centre_x - cross_centre_len, centre_y), cv::Scalar(255,255,255));
    cv::line(show_image, cv::Point(centre_x - cross_half_len, centre_y - 2), cv::Point(centre_x - cross_centre_len, centre_y - 2), cv::Scalar(0,0,0));
    cv::line(show_image, cv::Point(centre_x + cross_half_len, centre_y), cv::Point(centre_x + cross_centre_len, centre_y), cv::Scalar(255,255,255));
    cv::line(show_image, cv::Point(centre_x + cross_half_len, centre_y - 2), cv::Point(centre_x + cross_centre_len, centre_y-2), cv::Scalar(0,0,0));
    cv::line(show_image, cv::Point(centre_x, centre_y - cross_half_len), cv::Point(centre_x, centre_y - cross_centre_len), cv::Scalar(255,255,255));
    cv::line(show_image, cv::Point(centre_x -2, centre_y - cross_half_len), cv::Point(centre_x-2, centre_y - cross_centre_len), cv::Scalar(0,0,0));
    cv::line(show_image, cv::Point(centre_x, centre_y + cross_half_len), cv::Point(centre_x, centre_y + cross_centre_len), cv::Scalar(255,255,255));
    cv::line(show_image, cv::Point(centre_x-2, centre_y + cross_half_len), cv::Point(centre_x-2, centre_y + cross_half_len), cv::Scalar(0,0,0));
    cv::circle(show_image, cv::Point(centre_x, centre_y), 1, cv::Scalar(255,255,255), -1);
}

void getKjgDataCallBack(SIFU_CTR *s_ctr, CAM_DATA *c_data)
{
    if(c_data->data != nullptr) {
        show_image.data = c_data->data;
        
        if (tracker.is_initialized && getSensorDev() == 0) {
            cv::Rect tracked_roi = tracker.update(show_image);
            cv::rectangle(show_image, tracked_roi, cv::Scalar(0, 255, 0), 2);
            int center_x = tracked_roi.x + tracked_roi.width / 2;
            int center_y = tracked_roi.y + tracked_roi.height / 2;
            s_ctr->track_center_x = center_x - 1920 / 2;
            s_ctr->track_center_y = 1080 / 2 - center_y;
            s_ctr->track_status = 1;
        }
        
        if (need_init_tracker && getSensorDev() == 0) {
            printf("Initializing tracker on KJG\n");
            tracker.set_infrared_mode(false);
            tracker.init(show_image, selected_roi);
            need_init_tracker = false;
            printf("DSST tracker initialized on KJG\n");
        }
        
        show_osd(show_image);
    }
}

void getIrDataCallBack(SIFU_CTR *s_ctr, CAM_DATA *c_data)
{
    static int frame_count = 0;
    frame_count++;
    
    if(c_data->data != nullptr) {
        if(frame_count % 30 == 0) {
            printf("IR frame %d, tracker: %s\n", frame_count, tracker.is_initialized ? "YES" : "NO");
        }
        
        show_image_ir.data = c_data->data;
        
        if (tracker.is_initialized && getSensorDev() != 0) {
            cv::Rect tracked_roi = tracker.update(show_image_ir);
            cv::rectangle(show_image_ir, tracked_roi, cv::Scalar(0, 255, 0), 2);
            int center_x = tracked_roi.x + tracked_roi.width / 2;
            int center_y = tracked_roi.y + tracked_roi.height / 2;
            s_ctr->track_center_x = center_x - 640 / 2;
            s_ctr->track_center_y = 512 / 2 - center_y;
            s_ctr->track_status = 1;
        }
        
        if (need_init_tracker && getSensorDev() != 0) {
            printf("Initializing tracker on IR\n");
            tracker.set_infrared_mode(true);
            tracker.init(show_image_ir, selected_roi);
            need_init_tracker = false;
            printf("DSST tracker initialized on IR\n");
        }
        
        show_osd(show_image_ir);
    }
}

void getCMDCallBack(unsigned char *cmd_data, int len)
{
    printf("cmd len:%d\n", len);

    if((cmd_data != nullptr) && (len == 16)){
        int cmd = cmd_data[3];
        printf("Command type: 0x%02X\n", cmd);
        
        switch (cmd)
        {
        case 0x10:
            printf("stop tracker\n");
            tracker.is_initialized = false;
            has_selected_roi = false;
            need_init_tracker = false;
        break;
        case 0x11:
        {
            short *px = (short *)&cmd_data[4];
            short *py = (short *)&cmd_data[6];
            cv::Rect _track_init_box;
            if(getSensorDev() == 0) {
                _track_init_box.width = 64;
                _track_init_box.height = 64;
                _track_init_box.x = *px + 1920/2 - _track_init_box.width/2;
                _track_init_box.y = 1080/2 - _track_init_box.height/2 - *py;
            } else {
                _track_init_box.width = 44;
                _track_init_box.height = 44;
                _track_init_box.x = *px + 640/2 - _track_init_box.width/2;
                _track_init_box.y = 512/2 - _track_init_box.height/2 - *py;
            }
            
            bool is_infrared = (getSensorDev() != 0);
            tracker.set_infrared_mode(is_infrared);
            printf("Tracking mode: %s\n", is_infrared ? "infrared" : "visible");
            
            if(getSensorDev() == 0) {
                _track_init_box.x = (0 > _track_init_box.x ? 0 : _track_init_box.x);
                _track_init_box.y = (0 > _track_init_box.y ? 0 : _track_init_box.y);
                _track_init_box.width = (1920 - _track_init_box.x < _track_init_box.width ? 1920 - _track_init_box.x : _track_init_box.width);
                _track_init_box.height = (1080 - _track_init_box.y < _track_init_box.height ? 1080 - _track_init_box.y : _track_init_box.height);
            } else {
                _track_init_box.x = (0 > _track_init_box.x ? 0 : _track_init_box.x);
                _track_init_box.y = (0 > _track_init_box.y ? 0 : _track_init_box.y);
                _track_init_box.width = (640 - _track_init_box.x < _track_init_box.width ? 640 - _track_init_box.x : _track_init_box.width);
                _track_init_box.height = (512 - _track_init_box.y < _track_init_box.height ? 512 - _track_init_box.y : _track_init_box.height);
            }
            
            printf("start tracker xywh:%d %d %d %d\n", _track_init_box.x, _track_init_box.y, _track_init_box.width, _track_init_box.height);
            
            selected_roi = _track_init_box;
            has_selected_roi = true;
            need_init_tracker = true;
        }
        break;
        case 0x12:
        {
            short *px = (short *)&cmd_data[4];
            short *py = (short *)&cmd_data[6];
            cv::Rect _track_init_box;
            if(getSensorDev() == 0) {
                _track_init_box.width = 48;
                _track_init_box.height = 48;
                _track_init_box.x = *px + 1920/2 - _track_init_box.width/2;
                _track_init_box.y = 1080/2 - _track_init_box.height/2 - *py;
            } else {
                _track_init_box.width = 28;
                _track_init_box.height = 28;
                _track_init_box.x = *px + 640/2 - _track_init_box.width/2;
                _track_init_box.y = 512/2 - _track_init_box.height/2 - *py;
            }
            
            bool is_infrared = (getSensorDev() != 0);
            tracker.set_infrared_mode(is_infrared);
            printf("Tracking mode: %s\n", is_infrared ? "infrared" : "visible");
            
            if(getSensorDev() == 0) {
                _track_init_box.x = (0 > _track_init_box.x ? 0 : _track_init_box.x);
                _track_init_box.y = (0 > _track_init_box.y ? 0 : _track_init_box.y);
                _track_init_box.width = (1920 - _track_init_box.x < _track_init_box.width ? 1920 - _track_init_box.x : _track_init_box.width);
                _track_init_box.height = (1080 - _track_init_box.y < _track_init_box.height ? 1080 - _track_init_box.y : _track_init_box.height);
            } else {
                _track_init_box.x = (0 > _track_init_box.x ? 0 : _track_init_box.x);
                _track_init_box.y = (0 > _track_init_box.y ? 0 : _track_init_box.y);
                _track_init_box.width = (640 - _track_init_box.x < _track_init_box.width ? 640 - _track_init_box.x : _track_init_box.width);
                _track_init_box.height = (512 - _track_init_box.y < _track_init_box.height ? 512 - _track_init_box.y : _track_init_box.height);
            }
            
            printf("start tracker xywh:%d %d %d %d\n", _track_init_box.x, _track_init_box.y, _track_init_box.width, _track_init_box.height);
            
            selected_roi = _track_init_box;
            has_selected_roi = true;
            need_init_tracker = true;
        }
        break;
        case 0x13:
        {
            short *px = (short *)&cmd_data[4];
            short *py = (short *)&cmd_data[6];
            cv::Rect _track_init_box;
            if(getSensorDev() == 0) {
                _track_init_box.width = 32;
                _track_init_box.height = 32;
                _track_init_box.x = *px + 1920/2 - _track_init_box.width/2;
                _track_init_box.y = 1080/2 - _track_init_box.height/2 - *py;
            } else {
                _track_init_box.width = 12;
                _track_init_box.height = 12;
                _track_init_box.x = *px + 640/2 - _track_init_box.width/2;
                _track_init_box.y = 512/2 - _track_init_box.height/2 - *py;
            }
            
            bool is_infrared = (getSensorDev() != 0);
            tracker.set_infrared_mode(is_infrared);
            printf("Tracking mode: %s\n", is_infrared ? "infrared" : "visible");
            
            if(getSensorDev() == 0) {
                _track_init_box.x = (0 > _track_init_box.x ? 0 : _track_init_box.x);
                _track_init_box.y = (0 > _track_init_box.y ? 0 : _track_init_box.y);
                _track_init_box.width = (1920 - _track_init_box.x < _track_init_box.width ? 1920 - _track_init_box.x : _track_init_box.width);
                _track_init_box.height = (1080 - _track_init_box.y < _track_init_box.height ? 1080 - _track_init_box.y : _track_init_box.height);
            } else {
                _track_init_box.x = (0 > _track_init_box.x ? 0 : _track_init_box.x);
                _track_init_box.y = (0 > _track_init_box.y ? 0 : _track_init_box.y);
                _track_init_box.width = (640 - _track_init_box.x < _track_init_box.width ? 640 - _track_init_box.x : _track_init_box.width);
                _track_init_box.height = (512 - _track_init_box.y < _track_init_box.height ? 512 - _track_init_box.y : _track_init_box.height);
            }
            
            printf("start tracker xywh:%d %d %d %d\n", _track_init_box.x, _track_init_box.y, _track_init_box.width, _track_init_box.height);
            
            selected_roi = _track_init_box;
            has_selected_roi = true;
            need_init_tracker = true;
        }
        break;
        case 0x60:
            if(cmd_data[4] == 0x48) {
                short *x = (short *)&cmd_data[5];
                short *y = (short *)&cmd_data[7];
                short *w = (short *)&cmd_data[9];
                short *h = (short *)&cmd_data[11];
                cv::Rect _track_init_box;
                _track_init_box.x = *x;
                _track_init_box.y = *y;
                _track_init_box.width = *w;
                _track_init_box.height = *h;
                
                bool is_infrared = (getSensorDev() != 0);
                tracker.set_infrared_mode(is_infrared);
                printf("Tracking mode: %s\n", is_infrared ? "infrared" : "visible");
                
                if(getSensorDev() == 0) {
                    _track_init_box.x = (0 > _track_init_box.x ? 0 : _track_init_box.x);
                    _track_init_box.y = (0 > _track_init_box.y ? 0 : _track_init_box.y);
                    _track_init_box.width = (1920 - _track_init_box.x < _track_init_box.width ? 1920 - _track_init_box.x : _track_init_box.width);
                    _track_init_box.height = (1080 - _track_init_box.y < _track_init_box.height ? 1080 - _track_init_box.y : _track_init_box.height);
                } else {
                    _track_init_box.x = (0 > _track_init_box.x ? 0 : _track_init_box.x);
                    _track_init_box.y = (0 > _track_init_box.y ? 0 : _track_init_box.y);
                    _track_init_box.width = (640 - _track_init_box.x < _track_init_box.width ? 640 - _track_init_box.x : _track_init_box.width);
                    _track_init_box.height = (512 - _track_init_box.y < _track_init_box.height ? 512 - _track_init_box.y : _track_init_box.height);
                }
                
                printf("start tracker xywh:%d %d %d %d\n", _track_init_box.x, _track_init_box.y, _track_init_box.width, _track_init_box.height);
                
                selected_roi = _track_init_box;
                has_selected_roi = true;
                need_init_tracker = true;
            }
        break;
        case 0x84:
        {
            if(cmd_data[4] == 0) {
                printf("Trigger source: visible (1920*1080)\n");
            } else {
                printf("Trigger source: infrared (640*512)\n");
            }
            
            short *x = (short *)&cmd_data[5];
            short *y = (short *)&cmd_data[7];
            short *w = (short *)&cmd_data[9];
            short *h = (short *)&cmd_data[11];
            
            bool is_infrared = (cmd_data[4] != 0);
            tracker.set_infrared_mode(is_infrared);
            printf("Tracking mode: %s\n", is_infrared ? "infrared" : "visible");
            
            cv::Rect _track_init_box;
            _track_init_box.x = *x;
            _track_init_box.y = *y;
            _track_init_box.width = *w;
            _track_init_box.height = *h;
            
            int image_width = is_infrared ? 640 : 1920;
            int image_height = is_infrared ? 512 : 1080;
            _track_init_box.x = (0 > _track_init_box.x ? 0 : _track_init_box.x);
            _track_init_box.y = (0 > _track_init_box.y ? 0 : _track_init_box.y);
            _track_init_box.width = (image_width - _track_init_box.x < _track_init_box.width ? image_width - _track_init_box.x : _track_init_box.width);
            _track_init_box.height = (image_height - _track_init_box.y < _track_init_box.height ? image_height - _track_init_box.y : _track_init_box.height);
            
            printf("0x84 start tracker xywh:%d %d %d %d\n", _track_init_box.x, _track_init_box.y, _track_init_box.width, _track_init_box.height);
            
            selected_roi = _track_init_box;
            has_selected_roi = true;
            need_init_tracker = true;
        }
        break;
        default:
            break;
        }
    }
}

int main()
{
    printf("=== DSST Tracker Program Starting ===\n");
    printf("Using dsst_tracker with GST algorithm\n");
    printf("Supports commands: 0x10(stop), 0x11/0x12/0x13(start), 0x60/0x48/0x84(select)\n\n");
    
    gz0084StreamInit();
    kjgSetDataCallBack(getKjgDataCallBack);
    irSetDataCallBack(getIrDataCallBack);
    uartSetCmdCallBack(getCMDCallBack);
    
    while (1)
    {
        usleep(30000);
    }
    
    return 0;
}
