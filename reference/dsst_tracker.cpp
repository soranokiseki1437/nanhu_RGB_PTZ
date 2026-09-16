#include "dsst_tracker.h"

Mat gst_process(const Mat& img) {
    float sigma1 = 0.3f;
    float sigma2 = 0.3f;
    int boundaryWidth = 5;
    int filterSize = 5;
    
    Mat gray;
    if (img.channels() == 3) {
        cvtColor(img, gray, COLOR_BGR2GRAY);
    } else {
        gray = img;
    }
    gray.convertTo(gray, CV_64F);
    
    int mdpt = ceil(filterSize / 2.0);
    int actual_filter_size = 2 * mdpt + 1;
    
    Mat F1_real(actual_filter_size, actual_filter_size, CV_64F);
    Mat F1_imag(actual_filter_size, actual_filter_size, CV_64F);
    for (int y = -mdpt; y <= mdpt; y++) {
        for (int x = -mdpt; x <= mdpt; x++) {
            double r_sq = x * x + y * y;
            double exp_val = exp(-r_sq / (2.0 * sigma1 * sigma1));
            double coeff = pow(-1.0 / (sigma1 * sigma1), 1.0) / (2.0 * CV_PI * sigma1 * sigma1);
            F1_real.at<double>(y + mdpt, x + mdpt) = coeff * x * exp_val;
            F1_imag.at<double>(y + mdpt, x + mdpt) = coeff * y * exp_val;
        }
    }
    
    double sum_F1 = 0;
    for (int i = 0; i < filterSize; i++) {
        for (int j = 0; j < filterSize; j++) {
            sum_F1 += sqrt(F1_real.at<double>(i, j) * F1_real.at<double>(i, j) + 
                          F1_imag.at<double>(i, j) * F1_imag.at<double>(i, j));
        }
    }
    F1_real = F1_real / sum_F1;
    F1_imag = F1_imag / sum_F1;
    
    Mat f1_real, f1_imag;
    filter2D(gray, f1_real, CV_64F, F1_real, Point(-1, -1), 0, BORDER_REFLECT);
    filter2D(gray, f1_imag, CV_64F, F1_imag, Point(-1, -1), 0, BORDER_REFLECT);
    
    Mat Z = (f1_real.mul(f1_real) + f1_imag.mul(f1_imag)) / 255.0;
    
    Mat h_real(actual_filter_size, actual_filter_size, CV_64F);
    Mat h_imag(actual_filter_size, actual_filter_size, CV_64F);
    for (int y = -mdpt; y <= mdpt; y++) {
        for (int x = -mdpt; x <= mdpt; x++) {
            double r_sq = x * x + y * y;
            double exp_val = exp(-r_sq / (2.0 * sigma2 * sigma2));
            double coeff = pow(-1.0 / (sigma2 * sigma2), 2.0) / (2.0 * CV_PI * sigma2 * sigma2);
            double x_minus_iy_real = x * x - y * y;
            double x_minus_iy_imag = -2.0 * x * y;
            h_real.at<double>(y + mdpt, x + mdpt) = coeff * x_minus_iy_real * exp_val;
            h_imag.at<double>(y + mdpt, x + mdpt) = coeff * x_minus_iy_imag * exp_val;
        }
    }
    
    double sum_h = 0;
    for (int i = 0; i < filterSize; i++) {
        for (int j = 0; j < filterSize; j++) {
            sum_h += sqrt(h_real.at<double>(i, j) * h_real.at<double>(i, j) + 
                         h_imag.at<double>(i, j) * h_imag.at<double>(i, j));
        }
    }
    h_real = h_real / sum_h;
    h_imag = h_imag / sum_h;
    
    Mat I20_real, I20_imag;
    filter2D(Z, I20_real, CV_64F, h_real, Point(-1, -1), 0, BORDER_REFLECT);
    filter2D(Z, I20_imag, CV_64F, h_imag, Point(-1, -1), 0, BORDER_REFLECT);
    
    Mat abs_Z = abs(Z);
    Mat abs_h = abs(h_real) + abs(h_imag);
    Mat I10;
    filter2D(abs_Z, I10, CV_64F, abs_h, Point(-1, -1), 0, BORDER_REFLECT);
    
    Mat Ct(I20_real.size(), CV_64F);
    for (int i = 0; i < I20_real.rows; i++) {
        for (int j = 0; j < I20_real.cols; j++) {
            double theta = 0.5 * atan2(I20_imag.at<double>(i, j), I20_real.at<double>(i, j));
            double coscs = cos(theta);
            double abs_I20 = sqrt(I20_real.at<double>(i, j) * I20_real.at<double>(i, j) + 
                                  I20_imag.at<double>(i, j) * I20_imag.at<double>(i, j));
            Ct.at<double>(i, j) = abs_I20 * I10.at<double>(i, j) * (1.0 + coscs);
        }
    }
    
    Mat revised_Ct = Mat::zeros(Ct.size(), CV_64F);
    if (Ct.rows > 2 * boundaryWidth && Ct.cols > 2 * boundaryWidth) {
        Ct(Rect(boundaryWidth, boundaryWidth, 
                Ct.cols - 2 * boundaryWidth, Ct.rows - 2 * boundaryWidth)).copyTo(
            revised_Ct(Rect(boundaryWidth, boundaryWidth, 
                           Ct.cols - 2 * boundaryWidth, Ct.rows - 2 * boundaryWidth)));
    } else {
        revised_Ct = Ct.clone();
    }
    
    revised_Ct.convertTo(revised_Ct, CV_32F);
    
    return revised_Ct;
}

Mat DSSTTracker::hann(int n) {
    Mat window(n, 1, CV_32F);
    for (int i = 0; i < n; i++) {
        window.at<float>(i, 0) = 0.5f * (1.0f - cos(2.0f * CV_PI * i / (n - 1)));
    }
    return window;
}

Mat DSSTTracker::gaussian_peak(Size sz, float sigma) {
    Mat y(sz, CV_32F);
    for (int i = 0; i < sz.height; i++) {
        for (int j = 0; j < sz.width; j++) {
            float x = j - sz.width / 2.0f;
            float y_ = i - sz.height / 2.0f;
            y.at<float>(i, j) = exp(-0.5f * (x * x + y_ * y_) / (sigma * sigma));
        }
    }
    return y;
}

Mat DSSTTracker::complex_multiply(const Mat& a, const Mat& b) {
    if (a.empty() || b.empty()) {
        printf("Error: complex_multiply called with empty matrix\n");
        return Mat();
    }
    if (a.size() != b.size()) {
        printf("Matrix size mismatch: a(%d,%d), b(%d,%d)\n", a.rows, a.cols, b.rows, b.cols);
        return Mat();
    }
    if (a.channels() != 2 || b.channels() != 2) {
        printf("Error: complex_multiply called on non-complex matrix\n");
        return Mat();
    }
    Mat result(a.size(), a.type());
    for (int i = 0; i < a.rows; i++) {
        for (int j = 0; j < a.cols; j++) {
            Vec2f a_elem = a.at<Vec2f>(i, j);
            Vec2f b_elem = b.at<Vec2f>(i, j);
            result.at<Vec2f>(i, j)[0] = a_elem[0] * b_elem[0] - a_elem[1] * b_elem[1];
            result.at<Vec2f>(i, j)[1] = a_elem[0] * b_elem[1] + a_elem[1] * b_elem[0];
        }
    }
    return result;
}

Mat DSSTTracker::complex_conj(const Mat& a) {
    if (a.empty()) {
        printf("Error: complex_conj called with empty matrix\n");
        return Mat();
    }
    if (a.channels() != 2) {
        printf("Error: complex_conj called on non-complex matrix\n");
        return Mat();
    }
    Mat result(a.size(), a.type());
    for (int i = 0; i < a.rows; i++) {
        for (int j = 0; j < a.cols; j++) {
            Vec2f elem = a.at<Vec2f>(i, j);
            result.at<Vec2f>(i, j)[0] = elem[0];
            result.at<Vec2f>(i, j)[1] = -elem[1];
        }
    }
    return result;
}

Mat DSSTTracker::get_subwindow(const Mat& im, Point2f pos, Size model_sz, float scale) {
    if (im.empty()) {
        printf("Error: get_subwindow called with empty image\n");
        return Mat();
    }
    
    Size patch_sz = Size(floor(model_sz.width * scale), floor(model_sz.height * scale));
    
    if (patch_sz.width < 2) patch_sz.width = 2;
    if (patch_sz.height < 2) patch_sz.height = 2;
    
    int x1 = floor(pos.y) + 1 - patch_sz.height / 2;
    int y1 = floor(pos.x) + 1 - patch_sz.width / 2;
    int x2 = x1 + patch_sz.height - 1;
    int y2 = y1 + patch_sz.width - 1;
    
    x1 = max(1, x1);
    y1 = max(1, y1);
    x2 = min(im.rows, x2);
    y2 = min(im.cols, y2);
    
    if (x1 >= x2 || y1 >= y2) {
        printf("Error: Invalid ROI: x1=%d, x2=%d, y1=%d, y2=%d\n", x1, x2, y1, y2);
        return Mat();
    }
    
    Mat roi = im(Rect(y1 - 1, x1 - 1, y2 - y1 + 1, x2 - x1 + 1));
    
    if (roi.empty()) {
        printf("Error: Empty ROI extracted\n");
        return Mat();
    }
    
    Mat resized;
    resize(roi, resized, model_sz);
    
    if (resized.empty()) {
        printf("Error: Failed to resize ROI\n");
        return Mat();
    }
    
    return resized;
}

vector<Mat> DSSTTracker::get_feature_map(const Mat& im_patch) {
    vector<Mat> features;
    
    if (im_patch.empty()) {
        printf("Error: get_feature_map called with empty patch\n");
        return features;
    }
    
    if (is_infrared_mode) {
        Mat gst_features = gst_process(im_patch);
        if (gst_features.empty()) {
            printf("Error: gst_process returned empty features\n");
            return features;
        }
        Scalar mean_val, stddev;
        meanStdDev(gst_features, mean_val, stddev);
        gst_features = (gst_features - mean_val[0]) / (stddev[0] + 1e-10f);
        features.push_back(gst_features);
    } else {
        Mat gray;
        if (im_patch.channels() == 3) {
            cvtColor(im_patch, gray, COLOR_BGR2GRAY);
        } else {
            gray = im_patch;
        }
        gray.convertTo(gray, CV_32F);
        
        Mat norm_gray = gray.clone();
        Scalar mean_val, stddev;
        meanStdDev(norm_gray, mean_val, stddev);
        norm_gray = (norm_gray - mean_val[0]) / (stddev[0] + 1e-10f);
        features.push_back(norm_gray);
        
        Mat grad_x;
        Sobel(gray, grad_x, CV_32F, 1, 0, 3);
        meanStdDev(grad_x, mean_val, stddev);
        grad_x = (grad_x - mean_val[0]) / (stddev[0] + 1e-10f);
        features.push_back(grad_x);
        
        Mat grad_y;
        Sobel(gray, grad_y, CV_32F, 0, 1, 3);
        meanStdDev(grad_y, mean_val, stddev);
        grad_y = (grad_y - mean_val[0]) / (stddev[0] + 1e-10f);
        features.push_back(grad_y);
    }
    
    printf("Feature map created with %d channels\n", (int)features.size());
    return features;
}

vector<Mat> DSSTTracker::get_translation_sample(const Mat& im, Point2f pos, Size model_sz, float currentScaleFactor, const vector<Mat>& cos_window) {
    Mat im_patch = get_subwindow(im, pos, model_sz, currentScaleFactor);
    if (im_patch.empty()) {
        printf("Error: get_subwindow returned empty patch\n");
        return vector<Mat>();
    }
    
    vector<Mat> features = get_feature_map(im_patch);
    if (features.empty()) {
        printf("Error: get_feature_map returned empty features\n");
        return vector<Mat>();
    }
    
    for (int d = 0; d < features.size(); d++) {
        if (d >= cos_window.size() || features[d].size() != cos_window[d].size()) {
            printf("Error: cos_window size mismatch for channel %d\n", d);
            return vector<Mat>();
        }
        features[d] = features[d].mul(cos_window[d]);
    }
    
    return features;
}

Mat DSSTTracker::get_scale_sample(const Mat& im, Point2f pos, Size base_target_sz, 
                                 const vector<float>& scaleFactors, const Mat& scale_window, 
                                 Size scale_model_sz) {
    if (im.empty()) {
        printf("Error: get_scale_sample called with empty image\n");
        return Mat();
    }
    
    int nScales = scaleFactors.size();
    printf("get_scale_sample: nScales=%d, scale_model_sz=(%d,%d), area=%d\n", 
           nScales, scale_model_sz.width, scale_model_sz.height, (int)scale_model_sz.area());
    
    Mat out(scale_model_sz.area(), nScales, CV_32F);
    
    for (int s = 0; s < nScales; s++) {
        Size patch_sz = Size(floor(base_target_sz.width * scaleFactors[s]), 
                             floor(base_target_sz.height * scaleFactors[s]));
        
        int x1 = floor(pos.y) + 1 - patch_sz.height / 2;
        int y1 = floor(pos.x) + 1 - patch_sz.width / 2;
        int x2 = x1 + patch_sz.height - 1;
        int y2 = y1 + patch_sz.width - 1;
        
        x1 = max(1, x1);
        y1 = max(1, y1);
        x2 = min(im.rows, x2);
        y2 = min(im.cols, y2);
        
        if (x2 > x1 && y2 > y1) {
            Mat im_patch = im(Rect(y1 - 1, x1 - 1, y2 - y1 + 1, x2 - x1 + 1));
            Mat resized;
            resize(im_patch, resized, scale_model_sz);
            
            Mat gray;
            if (is_infrared_mode) {
                gray = gst_process(resized);
            } else {
                if (resized.channels() == 3) {
                    cvtColor(resized, gray, COLOR_BGR2GRAY);
                } else {
                    gray = resized;
                }
                gray.convertTo(gray, CV_32F);
            }
            
            Scalar mean_val, stddev;
            meanStdDev(gray, mean_val, stddev);
            gray = (gray - mean_val[0]) / (stddev[0] + 1e-10f);
            
            Mat flat = gray.reshape(1, 1).t();
            
            float window_val = 1.0f;
            if (s < scale_window.rows) {
                window_val = scale_window.at<float>(s, 0);
            }
            
            if (flat.rows == out.rows) {
                for (int i = 0; i < out.rows; i++) {
                    out.at<float>(i, s) = flat.at<float>(i, 0) * window_val;
                }
            }
        }
    }
    
    printf("get_scale_sample output size: %d x %d\n", out.rows, out.cols);
    return out;
}

void DSSTTracker::set_infrared_mode(bool infrared) {
    is_infrared_mode = infrared;
    num_features = is_infrared_mode ? 1 : 3;
}

void DSSTTracker::init(const Mat& image, const Rect& bounding_box) {
    pos = Point2f(bounding_box.x + bounding_box.width / 2.0f, 
                 bounding_box.y + bounding_box.height / 2.0f);
    base_target_sz = Size(bounding_box.width, bounding_box.height);
    current_scale_factor = 1.0f;
    roi = bounding_box;
    
    model_sz = Size(floor(base_target_sz.width * (1 + padding)), 
                   floor(base_target_sz.height * (1 + padding)));
    
    float output_sigma = sqrt((float)(base_target_sz.width * base_target_sz.height)) * output_sigma_factor;
    Mat y = gaussian_peak(model_sz, output_sigma);
    dft(y, yf, DFT_COMPLEX_OUTPUT);
    
    cos_window.resize(num_features);
    Mat hann_h = hann(model_sz.width);
    Mat hann_v = hann(model_sz.height);
    Mat single_cos_window = hann_v * hann_h.t();
    for (int d = 0; d < num_features; d++) {
        cos_window[d] = single_cos_window.clone();
    }
    
    scaleFactors.resize(number_of_scales);
    for (int s = 0; s < number_of_scales; s++) {
        scaleFactors[s] = pow(scale_step, ceil(number_of_scales / 2.0f) - s - 1);
    }
    
    float scale_sigma = number_of_scales / sqrt(33.0f) * scale_sigma_factor;
    Mat ys(1, number_of_scales, CV_32F);
    for (int s = 0; s < number_of_scales; s++) {
        float x = (s + 1) - ceil(number_of_scales / 2.0f);
        ys.at<float>(0, s) = exp(-0.5f * x * x / (scale_sigma * scale_sigma));
    }
    
    Mat ys_complex;
    Mat planes[] = {ys, Mat::zeros(ys.size(), CV_32F)};
    merge(planes, 2, ys_complex);
    dft(ys_complex, ysf, DFT_COMPLEX_OUTPUT);
    printf("ysf initialized to size: %d x %d\n", ysf.rows, ysf.cols);
    
    if (number_of_scales % 2 == 0) {
        scale_cos_window = hann(number_of_scales + 1);
        scale_cos_window = scale_cos_window.rowRange(1, number_of_scales + 1);
    } else {
        scale_cos_window = hann(number_of_scales);
    }
    
    float scale_model_factor = 1.0f;
    if (base_target_sz.area() > scale_model_max_area) {
        scale_model_factor = sqrt(scale_model_max_area / base_target_sz.area());
    }
    scale_model_sz = Size(floor(base_target_sz.width * scale_model_factor), 
                          floor(base_target_sz.height * scale_model_factor));
    
    min_scale_factor = pow(scale_step, ceil(log(max(5.0f / model_sz.width, 5.0f / model_sz.height)) / log(scale_step)));
    max_scale_factor = pow(scale_step, floor(log(min((float)image.rows / base_target_sz.height, 
                                                     (float)image.cols / base_target_sz.width)) / log(scale_step)));
    
    vector<Mat> xl = get_translation_sample(image, pos, model_sz, current_scale_factor, cos_window);
    if (xl.empty()) {
        printf("Error: get_translation_sample returned empty features\n");
        return;
    }
    
    if (xl.size() != num_features) {
        printf("Warning: Feature channels mismatch: expected %d, got %d\n", num_features, (int)xl.size());
        num_features = xl.size();
        
        cos_window.resize(num_features);
        Mat hann_h = hann(model_sz.width);
        Mat hann_v = hann(model_sz.height);
        Mat single_cos_window = hann_v * hann_h.t();
        for (int d = 0; d < num_features; d++) {
            cos_window[d] = single_cos_window.clone();
        }
        
        xl = get_translation_sample(image, pos, model_sz, current_scale_factor, cos_window);
        if (xl.empty()) {
            printf("Error: get_translation_sample returned empty features after cos_window update\n");
            return;
        }
    }
    
    hf_num.resize(num_features);
    hf_den = Mat::zeros(model_sz, CV_32F);
    
    for (int d = 0; d < num_features; d++) {
        printf("Processing channel %d\n", d);
        if (d >= xl.size()) {
            printf("Error: Channel index %d out of bounds\n", d);
            return;
        }
        printf("  Input size: %d x %d\n", xl[d].rows, xl[d].cols);
        
        Mat xlf;
        try {
            dft(xl[d], xlf, DFT_COMPLEX_OUTPUT);
            printf("  DFT successful, output size: %d x %d\n", xlf.rows, xlf.cols);
        } catch (const cv::Exception& e) {
            printf("  DFT failed: %s\n", e.what());
            return;
        }
        
        Mat conj_xlf = complex_conj(xlf);
        if (conj_xlf.empty()) {
            printf("  complex_conj failed\n");
            return;
        }
        
        printf("  yf size: %d x %d\n", yf.rows, yf.cols);
        printf("  conj_xlf size: %d x %d\n", conj_xlf.rows, conj_xlf.cols);
        
        Mat new_hf_num_d = complex_multiply(yf, conj_xlf);
        if (new_hf_num_d.empty()) {
            printf("Failed to compute hf_num for channel %d\n", d);
            return;
        }
        hf_num[d] = new_hf_num_d;
        
        Mat mag_sq(xlf.size(), CV_32F);
        for (int i = 0; i < xlf.rows; i++) {
            for (int j = 0; j < xlf.cols; j++) {
                Vec2f elem = xlf.at<Vec2f>(i, j);
                float mag = elem[0] * elem[0] + elem[1] * elem[1];
                mag_sq.at<float>(i, j) = mag;
            }
        }
        hf_den += mag_sq;
    }
    
    Mat xs = get_scale_sample(image, pos, base_target_sz, scaleFactors, scale_cos_window, scale_model_sz);
    Mat xsf;
    if (!xs.empty()) {
        printf("xs size: %d x %d\n", xs.rows, xs.cols);
        printf("ysf size before reshape: %d x %d\n", ysf.rows, ysf.cols);
        
        // Debug: print ysf values
        printf("ysf(0)=(%.4f,%.4f), ysf(16)=(%.4f,%.4f)\n",
               ysf.at<Vec2f>(0,0)[0], ysf.at<Vec2f>(0,0)[1],
               ysf.at<Vec2f>(0,16)[0], ysf.at<Vec2f>(0,16)[1]);
        
        // Debug: print xs values
        printf("xs(0,0)=%.4f, xs(0,16)=%.4f\n", 
               xs.at<float>(0,0), xs.at<float>(0,16));
        
        if (ysf.rows != 1 || ysf.cols != number_of_scales) {
            printf("Reshaping ysf to (1, %d)\n", number_of_scales);
            ysf = ysf.reshape(0, 1);
            if (ysf.cols != number_of_scales) {
                ysf.create(1, number_of_scales, CV_32FC2);
            }
        }
        
        // FFT along each row (across scales) - matching MATLAB fft(xs,[],2)
        xsf.create(xs.rows, xs.cols, CV_32FC2);
        for (int row = 0; row < xs.rows; row++) {
            Mat row_vec = xs.row(row);
            Mat row_complex;
            Mat planes[] = {row_vec, Mat::zeros(row_vec.size(), CV_32F)};
            merge(planes, 2, row_complex);
            dft(row_complex, xsf.row(row), DFT_COMPLEX_OUTPUT);
        }
        printf("xsf size: %d x %d\n", xsf.rows, xsf.cols);
        
        // Debug: print xsf values
        printf("xsf(0,0)=(%.4f,%.4f), xsf(0,16)=(%.4f,%.4f)\n",
               xsf.at<Vec2f>(0,0)[0], xsf.at<Vec2f>(0,0)[1],
               xsf.at<Vec2f>(0,16)[0], xsf.at<Vec2f>(0,16)[1]);
        
        // sf_num = ysf .* conj(xsf) - element-wise multiply
        sf_num.create(xsf.rows, xsf.cols, CV_32FC2);
        for (int row = 0; row < xsf.rows; row++) {
            for (int col = 0; col < xsf.cols; col++) {
                Vec2f ysf_elem = ysf.at<Vec2f>(0, col);
                Vec2f xsf_elem = xsf.at<Vec2f>(row, col);
                Vec2f conj_xsf_elem(xsf_elem[0], -xsf_elem[1]);
                
                float real_part = ysf_elem[0] * conj_xsf_elem[0] - ysf_elem[1] * conj_xsf_elem[1];
                float imag_part = ysf_elem[0] * conj_xsf_elem[1] + ysf_elem[1] * conj_xsf_elem[0];
                sf_num.at<Vec2f>(row, col) = Vec2f(real_part, imag_part);
            }
        }
        
        // sf_den = sum(xsf .* conj(xsf), 1) - sum along rows
        sf_den = Mat::zeros(1, xsf.cols, CV_32F);
        for (int col = 0; col < xsf.cols; col++) {
            float sum = 0.0f;
            for (int row = 0; row < xsf.rows; row++) {
                Vec2f elem = xsf.at<Vec2f>(row, col);
                float mag = elem[0] * elem[0] + elem[1] * elem[1];
                sum += mag;
            }
            sf_den.at<float>(0, col) = sum;
        }
        printf("sf_num size: %d x %d, sf_den size: %d x %d\n", 
               sf_num.rows, sf_num.cols, sf_den.rows, sf_den.cols);
    } else {
        printf("Warning: get_scale_sample returned empty, skipping scale model training\n");
    }
    
    is_initialized = true;
    printf("Standard DSST tracker initialized: %s mode, pos=(%.1f, %.1f), size=(%d, %d)\n", 
           is_infrared_mode ? "infrared(GST)" : "visible(HOG)",
           pos.x, pos.y, base_target_sz.width, base_target_sz.height);
}

Rect DSSTTracker::update(const Mat& image) {
    if (!is_initialized) {
        return roi;
    }
    
    vector<Mat> xt = get_translation_sample(image, pos, model_sz, current_scale_factor, cos_window);
    
    Mat response = Mat::zeros(model_sz, CV_32F);
    
    for (int d = 0; d < num_features; d++) {
        if (d >= xt.size()) {
            printf("Warning: Channel %d out of bounds in position estimation\n", d);
            break;
        }
        Mat xtf;
        try {
            dft(xt[d], xtf, DFT_COMPLEX_OUTPUT);
        } catch (const cv::Exception& e) {
            printf("DFT failed in position estimation for channel %d: %s\n", d, e.what());
            break;
        }
        
        Mat responsef = complex_multiply(hf_num[d], xtf);
        if (responsef.empty()) {
            printf("Failed to compute response for channel %d\n", d);
            break;
        }
        
        Mat denominator;
        hf_den.convertTo(denominator, CV_32F);
        denominator += lambda;
        
        Mat response_d_complex(responsef.size(), CV_32FC2);
        for (int i = 0; i < responsef.rows; i++) {
            for (int j = 0; j < responsef.cols; j++) {
                Vec2f elem = responsef.at<Vec2f>(i, j);
                float den = denominator.at<float>(i, j);
                if (fabs(den) > 1e-10f) {
                    response_d_complex.at<Vec2f>(i, j) = Vec2f(elem[0] / den, elem[1] / den);
                } else {
                    response_d_complex.at<Vec2f>(i, j) = Vec2f(0.0f, 0.0f);
                }
            }
        }
        
        Mat response_d;
        idft(response_d_complex, response_d, DFT_REAL_OUTPUT | DFT_SCALE);
        response += response_d;
    }
    
    Point max_loc;
    minMaxLoc(response, NULL, NULL, NULL, &max_loc);
    
    // Position update: pos = pos + current_scale_factor * ([-model_sz.width/2, -model_sz.height/2] + max_loc)
    // This matches MATLAB: pos = pos + currentScaleFactor * ([-model_sz(2)/2, -model_sz(1)/2] + max_loc);
    Point2f offset(-model_sz.width / 2.0f + max_loc.x,
                  -model_sz.height / 2.0f + max_loc.y);
    pos = pos + current_scale_factor * offset;
    
    // Debug: print position info
    static int dbg_frame = 0;
    dbg_frame++;
    if (dbg_frame <= 5 || dbg_frame % 100 == 0) {
        printf("[Pos] frame=%d: pos=(%.1f,%.1f), offset=(%.1f,%.1f), scale=%.4f, max_loc=(%d,%d)\n",
               dbg_frame, pos.x, pos.y, offset.x, offset.y, current_scale_factor,
               max_loc.x, max_loc.y);
        fflush(stdout);
    }
    
    // Scale estimation - following MATLAB dsst_gray.m implementation
    // xs = get_scale_sample(...) returns [num_pixels x nScales]
    // xsf = fft(xs, [], 2) - FFT along each row (across scales)
    // scale_response = real(ifft(sum(sf_num .* xsf, 1) ./ (sf_den + lambda)))
    
    // Compute current scale factors: currentScaleFactor * scaleFactors
    vector<float> current_scale_factors(number_of_scales);
    for (int s = 0; s < number_of_scales; s++) {
        current_scale_factors[s] = current_scale_factor * scaleFactors[s];
    }
    
    Mat xs_test = get_scale_sample(image, pos, base_target_sz, current_scale_factors, 
                                   scale_cos_window, scale_model_sz);
    
    if (!xs_test.empty() && !sf_num.empty()) {
        // Step 1: FFT along each row (across scales) - matching MATLAB fft(xs,[],2)
        // xs_test is [num_pixels x nScales], we FFT each row
        Mat xsf(xs_test.rows, xs_test.cols, CV_32FC2);
        for (int r = 0; r < xs_test.rows; r++) {
            Mat row = xs_test.row(r).clone();
            Mat row_complex;
            Mat planes[] = {row, Mat::zeros(row.size(), CV_32F)};
            merge(planes, 2, row_complex);
            dft(row_complex, xsf.row(r), DFT_COMPLEX_OUTPUT);
        }
        
        // Debug: print first few values
        if (dbg_frame <= 2) {
            printf("[Scale-Debug] frame=%d: xs_test(0,0)=%.4f, xsf(0,0)=(%.4f,%.4f)\n",
                   dbg_frame, xs_test.at<float>(0,0), 
                   xsf.at<Vec2f>(0,0)[0], xsf.at<Vec2f>(0,0)[1]);
            printf("[Scale-Debug] sf_num(0,0)=(%.4f,%.4f), sf_den(0)=%.4f, lambda=%.4f\n",
                   sf_num.at<Vec2f>(0,0)[0], sf_num.at<Vec2f>(0,0)[1],
                   sf_den.at<float>(0,0), lambda);
        }
        
        // Step 2: Element-wise complex multiply: sf_num .* xsf
        Mat prod(xsf.rows, xsf.cols, CV_32FC2);
        for (int r = 0; r < xsf.rows; r++) {
            for (int c = 0; c < xsf.cols; c++) {
                Vec2f a = sf_num.at<Vec2f>(r, c);
                Vec2f b = xsf.at<Vec2f>(r, c);
                prod.at<Vec2f>(r, c) = Vec2f(a[0]*b[0] - a[1]*b[1], a[0]*b[1] + a[1]*b[0]);
            }
        }
        
        // Step 3: Sum along rows (dim 1 in MATLAB) -> [1 x nScales]
        Mat sum_prod(1, xsf.cols, CV_32FC2, Scalar(0, 0));
        for (int r = 0; r < prod.rows; r++) {
            for (int c = 0; c < prod.cols; c++) {
                Vec2f val = prod.at<Vec2f>(r, c);
                sum_prod.at<Vec2f>(0, c)[0] += val[0];
                sum_prod.at<Vec2f>(0, c)[1] += val[1];
            }
        }
        
        // Debug: print sum_prod
        if (dbg_frame <= 2) {
            printf("[Scale-Debug] sum_prod(0)=(%.4f,%.4f), sum_prod(16)=(%.4f,%.4f)\n",
                   sum_prod.at<Vec2f>(0,0)[0], sum_prod.at<Vec2f>(0,0)[1],
                   sum_prod.at<Vec2f>(0,16)[0], sum_prod.at<Vec2f>(0,16)[1]);
        }
        
        // Step 4: Divide by (sf_den + lambda)
        Mat scale_response_freq(1, xsf.cols, CV_32FC2);
        for (int c = 0; c < xsf.cols; c++) {
            float den = sf_den.at<float>(0, c) + lambda;
            scale_response_freq.at<Vec2f>(0, c)[0] = sum_prod.at<Vec2f>(0, c)[0] / den;
            scale_response_freq.at<Vec2f>(0, c)[1] = sum_prod.at<Vec2f>(0, c)[1] / den;
        }
        
        // Debug: print scale_response_freq
        if (dbg_frame <= 2) {
            printf("[Scale-Debug] scale_response_freq(0)=(%.4f,%.4f), (16)=(%.4f,%.4f)\n",
                   scale_response_freq.at<Vec2f>(0,0)[0], scale_response_freq.at<Vec2f>(0,0)[1],
                   scale_response_freq.at<Vec2f>(0,16)[0], scale_response_freq.at<Vec2f>(0,16)[1]);
        }
        
        // Step 5: IFFT to get time-domain response
        Mat scale_response_time;
        idft(scale_response_freq, scale_response_time, DFT_COMPLEX_OUTPUT | DFT_SCALE);
        
        // Debug: print scale_response_time
        if (dbg_frame <= 2) {
            printf("[Scale-Debug] scale_response_time(0)=(%.4f,%.4f), (16)=(%.4f,%.4f)\n",
                   scale_response_time.at<Vec2f>(0,0)[0], scale_response_time.at<Vec2f>(0,0)[1],
                   scale_response_time.at<Vec2f>(0,16)[0], scale_response_time.at<Vec2f>(0,16)[1]);
        }
        
        // Step 6: Extract real part
        Mat scale_response_real(1, number_of_scales, CV_32F);
        for (int s = 0; s < number_of_scales; s++) {
            Vec2f val = scale_response_time.at<Vec2f>(0, s);
            scale_response_real.at<float>(0, s) = val[0];
        }
        
        // Step 7: Find best scale (max response)
        Point max_loc;
        minMaxLoc(scale_response_real, NULL, NULL, NULL, &max_loc);
        int recovered_scale = max_loc.x;
        
        // Update scale factor
        float new_scale = current_scale_factor * scaleFactors[recovered_scale];
        
        // Debug output
        if (dbg_frame <= 5 || dbg_frame % 100 == 0) {
            printf("[Scale] frame=%d: best=%d(%.4f), scale=%.4f->%.4f\n",
                   dbg_frame, recovered_scale, scaleFactors[recovered_scale],
                   current_scale_factor, new_scale);
            if (dbg_frame <= 3) {
                printf("  scaleFactors: [");
                for (int i = 0; i < min(5, number_of_scales); i++) printf("%.3f ", scaleFactors[i]);
                printf("... ");
                for (int i = max(number_of_scales-3, 5); i < number_of_scales; i++) printf("%.3f ", scaleFactors[i]);
                printf("]\n");
                printf("  scale_response: [");
                for (int i = 0; i < min(5, number_of_scales); i++) printf("%.4f ", scale_response_real.at<float>(0, i));
                printf("... ");
                for (int i = max(number_of_scales-3, 5); i < number_of_scales; i++) printf("%.4f ", scale_response_real.at<float>(0, i));
                printf("]\n");
                printf("  center_idx=%d, center_response=%.4f\n", number_of_scales/2, scale_response_real.at<float>(0, number_of_scales/2));
            }
            fflush(stdout);
        }
        
        // Clamp scale factor
        if (new_scale < min_scale_factor) new_scale = min_scale_factor;
        if (new_scale > max_scale_factor) new_scale = max_scale_factor;
        current_scale_factor = new_scale;
    }
    
    vector<Mat> xl_new = get_translation_sample(image, pos, model_sz, current_scale_factor, cos_window);
    
    Mat new_hf_den = Mat::zeros(model_sz, CV_32F);
    
    for (int d = 0; d < num_features; d++) {
        if (d >= xl_new.size()) {
            printf("Warning: Channel %d out of bounds in update\n", d);
            break;
        }
        Mat xlf;
        try {
            dft(xl_new[d], xlf, DFT_COMPLEX_OUTPUT);
        } catch (const cv::Exception& e) {
            printf("DFT failed in update for channel %d: %s\n", d, e.what());
            continue;
        }
        
        Mat conj_xlf = complex_conj(xlf);
        Mat new_hf_num_d = complex_multiply(yf, conj_xlf);
        if (new_hf_num_d.empty()) {
            printf("Failed to compute new_hf_num for channel %d\n", d);
            continue;
        }
        
        Mat updated_hf_num = Mat::zeros(hf_num[d].size(), CV_32FC2);
        for (int i = 0; i < hf_num[d].rows; i++) {
            for (int j = 0; j < hf_num[d].cols; j++) {
                Vec2f old_elem = hf_num[d].at<Vec2f>(i, j);
                Vec2f new_elem = new_hf_num_d.at<Vec2f>(i, j);
                updated_hf_num.at<Vec2f>(i, j) = Vec2f(
                    (1 - learning_rate) * old_elem[0] + learning_rate * new_elem[0],
                    (1 - learning_rate) * old_elem[1] + learning_rate * new_elem[1]
                );
            }
        }
        hf_num[d] = updated_hf_num;
        
        Mat mag_sq(xlf.size(), CV_32F);
        for (int i = 0; i < xlf.rows; i++) {
            for (int j = 0; j < xlf.cols; j++) {
                Vec2f elem = xlf.at<Vec2f>(i, j);
                float mag = elem[0] * elem[0] + elem[1] * elem[1];
                mag_sq.at<float>(i, j) = mag;
            }
        }
        new_hf_den += mag_sq;
    }
    
    if (hf_den.size() == new_hf_den.size()) {
        hf_den = (1 - learning_rate) * hf_den + learning_rate * new_hf_den;
    } else {
        printf("Warning: hf_den and new_hf_den size mismatch, using new_hf_den\n");
        hf_den = new_hf_den.clone();
    }
    
    // Scale model update - following MATLAB dsst_gray.m
    // Recompute current_scale_factors with updated current_scale_factor
    for (int s = 0; s < number_of_scales; s++) {
        current_scale_factors[s] = current_scale_factor * scaleFactors[s];
    }
    Mat xs_train = get_scale_sample(image, pos, base_target_sz, current_scale_factors,
                                    scale_cos_window, scale_model_sz);
    if (!xs_train.empty() && !sf_num.empty()) {
        // FFT along each row (across scales) - matching MATLAB fft(xs,[],2)
        Mat xsf_train(xs_train.rows, xs_train.cols, CV_32FC2);
        for (int row = 0; row < xs_train.rows; row++) {
            Mat row_vec = xs_train.row(row);
            Mat row_complex;
            Mat planes[] = {row_vec, Mat::zeros(row_vec.size(), CV_32F)};
            merge(planes, 2, row_complex);
            dft(row_complex, xsf_train.row(row), DFT_COMPLEX_OUTPUT);
        }
        
        // new_sf_num = ysf .* conj(xsf_train) - element-wise multiply
        Mat new_sf_num(xsf_train.rows, xsf_train.cols, CV_32FC2);
        for (int row = 0; row < xsf_train.rows; row++) {
            for (int col = 0; col < xsf_train.cols; col++) {
                Vec2f ysf_elem = ysf.at<Vec2f>(0, col);
                Vec2f xsf_elem = xsf_train.at<Vec2f>(row, col);
                Vec2f conj_xsf_elem(xsf_elem[0], -xsf_elem[1]);
                
                float real_part = ysf_elem[0] * conj_xsf_elem[0] - ysf_elem[1] * conj_xsf_elem[1];
                float imag_part = ysf_elem[0] * conj_xsf_elem[1] + ysf_elem[1] * conj_xsf_elem[0];
                new_sf_num.at<Vec2f>(row, col) = Vec2f(real_part, imag_part);
            }
        }
        
        // new_sf_den = sum(xsf .* conj(xsf), 1) - sum along rows
        Mat new_sf_den(1, xsf_train.cols, CV_32F, Scalar(0));
        for (int col = 0; col < xsf_train.cols; col++) {
            float sum_mag = 0;
            for (int row = 0; row < xsf_train.rows; row++) {
                Vec2f elem = xsf_train.at<Vec2f>(row, col);
                sum_mag += elem[0] * elem[0] + elem[1] * elem[1];
            }
            new_sf_den.at<float>(0, col) = sum_mag;
        }
        
        // Update sf_num and sf_den with learning rate
        for (int r = 0; r < sf_num.rows; r++) {
            for (int c = 0; c < sf_num.cols; c++) {
                Vec2f old_val = sf_num.at<Vec2f>(r, c);
                Vec2f new_val = new_sf_num.at<Vec2f>(r, c);
                sf_num.at<Vec2f>(r, c) = Vec2f(
                    (1 - learning_rate) * old_val[0] + learning_rate * new_val[0],
                    (1 - learning_rate) * old_val[1] + learning_rate * new_val[1]
                );
            }
        }
        for (int c = 0; c < sf_den.cols; c++) {
            sf_den.at<float>(0, c) = (1 - learning_rate) * sf_den.at<float>(0, c) 
                                      + learning_rate * new_sf_den.at<float>(0, c);
        }
    }
    
    // Update target size based on current scale factor
    int width = floor(base_target_sz.width * current_scale_factor);
    int height = floor(base_target_sz.height * current_scale_factor);
    roi.x = pos.x - width / 2.0f;
    roi.y = pos.y - height / 2.0f;
    roi.width = width;
    roi.height = height;
    
    roi.x = max(0, roi.x);
    roi.y = max(0, roi.y);
    roi.width = min(image.cols - roi.x, roi.width);
    roi.height = min(image.rows - roi.y, roi.height);
    
    return roi;
}
