#ifndef FFTUTILS_H
#define FFTUTILS_H

#include <complex>
#include <vector>

// =============================================================================
// FFT 符号约定（重要！与 MATLAB / FFTW 的约定相反。
//   正向: X[k] = Σ_{n=0..N-1} x[n] * exp(+2πj·kn/N)
//   逆向: x[n] = (1/N) · Σ X[k] * exp(-2πj·kn/N)
// DSST 滤波器公式（hf_num/hf_den/sf_num/sf_den）与此约定绑定，
// 若替换 FFT 库需同步修正符号。
// =============================================================================

// 一维复数FFT (Cooley-Tukey)
void fft1d(std::vector<std::complex<float>>& data, bool inverse = false);

// 二维复数FFT
void fft2d(std::vector<std::vector<std::complex<float>>>& data, bool inverse = false);

// 实数矩阵转复数矩阵
std::vector<std::vector<std::complex<float>>> realToComplex(
    const std::vector<std::vector<float>>& realData);

// 复数矩阵取实部
std::vector<std::vector<float>> complexToReal(
    const std::vector<std::vector<std::complex<float>>>& complexData);

// 复数矩阵逐元素乘法 (Hadamard积)
std::vector<std::vector<std::complex<float>>> elementMultiply(
    const std::vector<std::vector<std::complex<float>>>& a,
    const std::vector<std::vector<std::complex<float>>>& b);

// 复数矩阵逐元素除法
std::vector<std::vector<std::complex<float>>> elementDivide(
    const std::vector<std::vector<std::complex<float>>>& num,
    const std::vector<std::vector<std::complex<float>>>& den,
    float lambda);

// 复数共轭
std::vector<std::vector<std::complex<float>>> conjugate(
    const std::vector<std::vector<std::complex<float>>>& data);

// 矩阵求和（所有元素之和）
float sumAll(const std::vector<std::vector<float>>& data);

// 生成Hann窗
std::vector<float> hannWindow(int size);

// 矩阵裁剪/填充到指定尺寸
std::vector<std::vector<float>> resizePatch(const std::vector<std::vector<float>>& patch,
                                             int targetRows, int targetCols);

// 从图像提取灰度patch（带边界处理）
// cx, cy 为浮点中心坐标；内部使用最近邻索引（image按像素存储）
// 调用方已在 KF 预测中得到浮点位置，这里统一 qRound 到最近像素
std::vector<std::vector<float>> extractGrayPatch(const std::vector<std::vector<float>>& image,
                                                  float cx, float cy, int w, int h);

// ========================
// FHOG 特征提取（Felzenszwalb HOG）
// 输入：灰度图像矩阵 (h x w)
// 输出：FHOG特征图 (h/binSize x w/binSize x 31)
// binSize=4, nOrients=9 → 每个cell输出31维特征
// ========================
struct FHOGConfig {
    int binSize = 4;        // cell大小（像素）
    int nOrients = 9;      // 方向直方图bin数
    float clip = 0.2f;     // 裁剪阈值
};

// 计算 FHOG 特征
// 返回: vector[h/binSize][w/binSize][31]，每个cell有31维特征
std::vector<std::vector<std::vector<float>>> computeFHOG(
    const std::vector<std::vector<float>>& grayImage,
    const FHOGConfig& config = FHOGConfig());

// 将 FHOG 3D 展平为 2D (cells × 31)，用于尺度滤波器输入
// 输入: [rows][cols][31] → 输出: [rows*cols][31]
std::vector<std::vector<float>> flattenFHOG(
    const std::vector<std::vector<std::vector<float>>>& fhog);

#endif // FFTUTILS_H
