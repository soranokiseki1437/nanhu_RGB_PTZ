// 仅用于离线对拍（reference 版）编译的空壳头文件
//
// reference/dsst_tracker.h 引用了 <opencv2/highgui.hpp>，但算法本身不使用其中任何函数
// （无 imshow/waitKey/namedWindow 等调用，已核对）。
// 本工程为减小体积只编译了 OpenCV 的 core+imgproc+imgcodecs 模块，
// 故在此提供空壳头，避免为了一个未使用的 include 去编译整个 highgui 模块。
// 注意：该目录只加到对拍程序的 include 路径里，不参与主工程编译。
#pragma once

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>