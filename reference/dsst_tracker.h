#ifndef DSST_TRACKER_H
#define DSST_TRACKER_H

#include <stdio.h>
#include <vector>
#include <complex>
#include "opencv2/opencv.hpp"
#include "opencv2/core.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/imgproc.hpp"

using namespace cv;
using namespace std;

Mat gst_process(const Mat& img);

class DSSTTracker {
private:
    float padding = 2.0f;
    float output_sigma_factor = 1.0f / 16.0f;
    float scale_sigma_factor = 1.0f / 4.0f;
    float lambda = 1e-2f;
    float learning_rate = 0.025f;
    int number_of_scales = 9;
    float scale_step = 1.05f;
    float scale_model_max_area = 512.0f;
    
    bool is_infrared_mode = true;
    int num_features = 3;
    
    Mat yf;
    vector<Mat> cos_window;
    vector<Mat> hf_num;
    Mat hf_den;
    
    Mat ysf;
    Mat scale_cos_window;
    Mat sf_num;
    Mat sf_den;
    
    Point2f pos;
    Size base_target_sz;
    Size model_sz;
    Size scale_model_sz;
    float current_scale_factor;
    vector<float> scaleFactors;
    float min_scale_factor;
    float max_scale_factor;
    Rect roi;
    
    Mat hann(int n);
    Mat gaussian_peak(Size sz, float sigma);
    Mat complex_multiply(const Mat& a, const Mat& b);
    Mat complex_conj(const Mat& a);
    Mat get_subwindow(const Mat& im, Point2f pos, Size model_sz, float scale);
    vector<Mat> get_feature_map(const Mat& im_patch);
    vector<Mat> get_translation_sample(const Mat& im, Point2f pos, Size model_sz, 
                                        float currentScaleFactor, const vector<Mat>& cos_window);
    Mat get_scale_sample(const Mat& im, Point2f pos, Size base_target_sz, 
                          const vector<float>& scaleFactors, const Mat& scale_window, 
                          Size scale_model_sz);
    
public:
    bool is_initialized = false;
    
    void set_infrared_mode(bool infrared);
    void init(const Mat& image, const Rect& bounding_box);
    Rect update(const Mat& image);
};

#endif
