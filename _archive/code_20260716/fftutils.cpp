#include "fftutils.h"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <QDebug>

// ========================
// 一维FFT (Cooley-Tukey迭代版)
// ========================
static void fft1dCore(std::vector<std::complex<float>>& a, bool inverse)
{
    int n = static_cast<int>(a.size());
    if (n <= 1) return;

    // 位反转重排
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }

    // 迭代FFT
    for (int len = 2; len <= n; len <<= 1) {
        float ang = 2.0f * static_cast<float>(M_PI) / len * (inverse ? -1.0f : 1.0f);
        std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (int j = 0; j < len / 2; ++j) {
                std::complex<float> u = a[i + j];
                std::complex<float> v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }

    if (inverse) {
        for (auto& x : a)
            x /= static_cast<float>(n);
    }
}

void fft1d(std::vector<std::complex<float>>& data, bool inverse)
{
    int origN = static_cast<int>(data.size());
    if (origN <= 1) return;

    // Cooley-Tukey 迭代版要求长度是 2 的幂；否则 padding 到下一个 2 的幂
    int n2 = 1;
    while (n2 < origN) n2 <<= 1;

    if (n2 == origN) {
        fft1dCore(data, inverse);
        return;
    }

    std::vector<std::complex<float>> padded(n2, std::complex<float>(0.0f, 0.0f));
    for (int i = 0; i < origN; ++i) padded[i] = data[i];
    fft1dCore(padded, inverse);

    if (inverse) {
        // 逆变换：返回原长度（前 origN 个点）
        for (int i = 0; i < origN; ++i) data[i] = padded[i];
    } else {
        // 正变换：返回原长度（频域采样点取前 origN 个；
        // 对于 MOSSE 相关滤波这类"单帧训练 + 相关"，正变换长度保持一致即可）
        for (int i = 0; i < origN; ++i) data[i] = padded[i];
    }
}

// ========================
// 二维FFT
// ========================
void fft2d(std::vector<std::vector<std::complex<float>>>& data, bool inverse)
{
    int rows = static_cast<int>(data.size());
    if (rows == 0) return;
    int cols = static_cast<int>(data[0].size());
    if (cols == 0) return;

    // 对每一行做FFT
    for (int r = 0; r < rows; ++r) {
        fft1d(data[r], inverse);
    }

    // 对每一列做FFT
    std::vector<std::complex<float>> col(rows);
    for (int c = 0; c < cols; ++c) {
        for (int r = 0; r < rows; ++r)
            col[r] = data[r][c];
        fft1d(col, inverse);
        for (int r = 0; r < rows; ++r)
            data[r][c] = col[r];
    }
}

// ========================
// 实数转复数
// ========================
std::vector<std::vector<std::complex<float>>> realToComplex(
    const std::vector<std::vector<float>>& realData)
{
    int rows = static_cast<int>(realData.size());
    int cols = rows > 0 ? static_cast<int>(realData[0].size()) : 0;
    std::vector<std::vector<std::complex<float>>> result(rows,
        std::vector<std::complex<float>>(cols));
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
            result[r][c] = std::complex<float>(realData[r][c], 0.0f);
    return result;
}

// ========================
// 复数取实部
// ========================
std::vector<std::vector<float>> complexToReal(
    const std::vector<std::vector<std::complex<float>>>& complexData)
{
    int rows = static_cast<int>(complexData.size());
    int cols = rows > 0 ? static_cast<int>(complexData[0].size()) : 0;
    std::vector<std::vector<float>> result(rows, std::vector<float>(cols));
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
            result[r][c] = complexData[r][c].real();
    return result;
}

// ========================
// 逐元素乘法
// ========================
std::vector<std::vector<std::complex<float>>> elementMultiply(
    const std::vector<std::vector<std::complex<float>>>& a,
    const std::vector<std::vector<std::complex<float>>>& b)
{
    int rows = static_cast<int>(a.size());
    int cols = rows > 0 ? static_cast<int>(a[0].size()) : 0;
    int bRows = static_cast<int>(b.size());
    int bCols = bRows > 0 ? static_cast<int>(b[0].size()) : 0;
    if (rows != bRows || cols != bCols) {
        qWarning() << "[elementMultiply] 尺寸不匹配: a=" << rows << "x" << cols
                     << " b=" << bRows << "x" << bCols;
        rows = std::min(rows, bRows);
        cols = std::min(cols, bCols);
    }
    std::vector<std::vector<std::complex<float>>> result(rows,
        std::vector<std::complex<float>>(cols, std::complex<float>(0.0f, 0.0f)));
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            auto val = a[r][c] * b[r][c];
            if (std::isnan(val.real()) || std::isinf(val.real()) ||
                std::isnan(val.imag()) || std::isinf(val.imag())) {
                val = std::complex<float>(0.0f, 0.0f);
            }
            result[r][c] = val;
        }
    }
    return result;
}

// ========================
// 逐元素除法 (num / (den + lambda))
// ========================
std::vector<std::vector<std::complex<float>>> elementDivide(
    const std::vector<std::vector<std::complex<float>>>& num,
    const std::vector<std::vector<std::complex<float>>>& den,
    float lambda)
{
    int rows = static_cast<int>(num.size());
    int cols = rows > 0 ? static_cast<int>(num[0].size()) : 0;
    int dRows = static_cast<int>(den.size());
    int dCols = dRows > 0 ? static_cast<int>(den[0].size()) : 0;
    if (rows != dRows || cols != dCols) {
        qWarning() << "[elementDivide] 尺寸不匹配: num=" << rows << "x" << cols
                     << " den=" << dRows << "x" << dCols;
        rows = std::min(rows, dRows);
        cols = std::min(cols, dCols);
    }
    std::vector<std::vector<std::complex<float>>> result(rows,
        std::vector<std::complex<float>>(cols, std::complex<float>(0.0f, 0.0f)));
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            float denom = den[r][c].real() + lambda;
            if (std::isnan(denom) || std::isinf(denom) || std::abs(denom) < 1e-10f) {
                denom = 1e-10f;
            }
            auto nv = num[r][c];
            if (std::isnan(nv.real()) || std::isinf(nv.real()) ||
                std::isnan(nv.imag()) || std::isinf(nv.imag())) {
                nv = std::complex<float>(0.0f, 0.0f);
            }
            result[r][c] = nv / denom;
        }
    }
    return result;
}

// ========================
// 共轭
// ========================
std::vector<std::vector<std::complex<float>>> conjugate(
    const std::vector<std::vector<std::complex<float>>>& data)
{
    int rows = static_cast<int>(data.size());
    int cols = rows > 0 ? static_cast<int>(data[0].size()) : 0;
    std::vector<std::vector<std::complex<float>>> result(rows,
        std::vector<std::complex<float>>(cols));
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
            result[r][c] = std::conj(data[r][c]);
    return result;
}

// ========================
// 矩阵求和
// ========================
float sumAll(const std::vector<std::vector<float>>& data)
{
    float sum = 0.0f;
    for (const auto& row : data)
        for (float v : row)
            sum += v;
    return sum;
}

// ========================
// Hann窗
// ========================
std::vector<float> hannWindow(int size)
{
    if (size <= 1) {
        return std::vector<float>(size, 1.0f);
    }
    std::vector<float> window(size);
    for (int i = 0; i < size; ++i) {
        window[i] = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * i / (size - 1)));
    }
    return window;
}

// ========================
// 双线性插值缩放（替代最近邻，保留更多细节）
// ========================
std::vector<std::vector<float>> resizePatch(const std::vector<std::vector<float>>& patch,
                                             int targetRows, int targetCols)
{
    int srcRows = static_cast<int>(patch.size());
    int srcCols = srcRows > 0 ? static_cast<int>(patch[0].size()) : 0;

    if (targetRows <= 0 || targetCols <= 0) {
        qWarning() << "[resizePatch] 无效目标尺寸:" << targetRows << "x" << targetCols;
        return {};
    }

    // 空输入保护：返回零填充结果
    if (srcRows == 0 || srcCols == 0) {
        qWarning() << "[resizePatch] 输入patch为空，返回零填充结果";
        return std::vector<std::vector<float>>(targetRows, std::vector<float>(targetCols, 0.0f));
    }

    if (srcRows == targetRows && srcCols == targetCols)
        return patch;

    std::vector<std::vector<float>> result(targetRows, std::vector<float>(targetCols, 0.0f));

    // 边界保护：单行/单列时退化为最近邻
    if (targetRows <= 1 || targetCols <= 1 || srcRows <= 1 || srcCols <= 1) {
        for (int r = 0; r < targetRows; ++r) {
            for (int c = 0; c < targetCols; ++c) {
                int sr = std::min((r * srcRows) / std::max(targetRows, 1), srcRows - 1);
                int sc = std::min((c * srcCols) / std::max(targetCols, 1), srcCols - 1);
                if (sr >= 0 && sr < srcRows && sc >= 0 && sc < static_cast<int>(patch[sr].size()))
                    result[r][c] = patch[sr][sc];
            }
        }
        return result;
    }

    for (int r = 0; r < targetRows; ++r) {
        float srcR_f = r * (srcRows - 1) / static_cast<float>(targetRows - 1);
        int srcR0 = static_cast<int>(srcR_f);
        int srcR1 = std::min(srcR0 + 1, srcRows - 1);
        float fracR = srcR_f - srcR0;

        for (int c = 0; c < targetCols; ++c) {
            float srcC_f = c * (srcCols - 1) / static_cast<float>(targetCols - 1);
            int srcC0 = static_cast<int>(srcC_f);
            int srcC1 = std::min(srcC0 + 1, srcCols - 1);
            float fracC = srcC_f - srcC0;

            // 双线性插值: f(r,c) = (1-fracR)*(1-fracC)*p00 + (1-fracR)*fracC*p01
            //                     + fracR*(1-fracC)*p10 + fracR*fracC*p11
            float p00 = patch[srcR0][srcC0];
            float p01 = patch[srcR0][srcC1];
            float p10 = patch[srcR1][srcC0];
            float p11 = patch[srcR1][srcC1];
            result[r][c] = (1.0f - fracR) * ((1.0f - fracC) * p00 + fracC * p01)
                          + fracR * ((1.0f - fracC) * p10 + fracC * p11);
        }
    }
    return result;
}

// ========================
// 提取灰度patch（边界用最近像素填充）
// 支持浮点中心坐标：KF 预测位置是浮点，统一 qRound 到最近像素
// ========================
std::vector<std::vector<float>> extractGrayPatch(const std::vector<std::vector<float>>& image,
                                                  float cx, float cy, int w, int h)
{
    int imgH = static_cast<int>(image.size());
    int imgW = imgH > 0 ? static_cast<int>(image[0].size()) : 0;

    // 空图像/无效尺寸保护
    if (imgH == 0 || imgW == 0 || w <= 0 || h <= 0) {
        qWarning() << "[extractGrayPatch] 空图像或无效尺寸: img="
                     << imgW << "x" << imgH << " patch=" << w << "x" << h;
        return std::vector<std::vector<float>>(std::max(h, 1), std::vector<float>(std::max(w, 1), 0.0f));
    }

    std::vector<std::vector<float>> patch(h, std::vector<float>(w, 0.0f));

    // NaN/溢出保护：如果中心坐标非法，使用图像中心
    if (std::isnan(cx) || std::isinf(cx)) cx = static_cast<float>(imgW) * 0.5f;
    if (std::isnan(cy) || std::isinf(cy)) cy = static_cast<float>(imgH) * 0.5f;
    // 额外范围限制，防止坐标过大导致 int 溢出
    cx = std::max(0.0f, std::min(static_cast<float>(imgW - 1), cx));
    cy = std::max(0.0f, std::min(static_cast<float>(imgH - 1), cy));

    int cxInt = static_cast<int>(std::round(cx));
    int cyInt = static_cast<int>(std::round(cy));
    int halfW = w / 2;
    int halfH = h / 2;

    for (int r = 0; r < h; ++r) {
        int imgR = cyInt - halfH + r;
        if (imgR < 0) imgR = 0;
        if (imgR >= imgH) imgR = imgH - 1;
        for (int c = 0; c < w; ++c) {
            int imgC = cxInt - halfW + c;
            if (imgC < 0) imgC = 0;
            if (imgC >= imgW) imgC = imgW - 1;
            patch[r][c] = image[imgR][imgC];
        }
    }
    return patch;
}

// ========================
// FHOG 特征提取实现
// ========================

static const float PI_F = 3.14159265358979323846f;

// 计算图像梯度（Gx, Gy, M, O）
static void computeGradients(const std::vector<std::vector<float>>& img,
                             std::vector<std::vector<float>>& gx,
                             std::vector<std::vector<float>>& gy,
                             std::vector<std::vector<float>>& mag,
                             std::vector<std::vector<float>>& ori)
{
    int h = static_cast<int>(img.size());
    int w = h > 0 ? static_cast<int>(img[0].size()) : 0;
    if (h < 2 || w < 2) return;

    gx.assign(h, std::vector<float>(w, 0.0f));
    gy.assign(h, std::vector<float>(w, 0.0f));
    mag.assign(h, std::vector<float>(w, 0.0f));
    ori.assign(h, std::vector<float>(w, 0.0f));

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            // Gx: 水平梯度
            float dxLeft = (x == 0) ? img[y][x+1] - img[y][x] : img[y][x] - img[y][x-1];
            float dxRight = (x == w-1) ? img[y][x] - img[y][x-1] : img[y][x+1] - img[y][x];
            float gxd = (x == 0 || x == w-1) ? dxRight : 0.5f * (dxRight + dxLeft);

            // Gy: 垂直梯度
            float dyUp = (y == 0) ? img[y+1][x] - img[y][x] : img[y][x] - img[y-1][x];
            float dyDown = (y == h-1) ? img[y][x] - img[y-1][x] : img[y+1][x] - img[y][x];
            float gyd = (y == 0 || y == h-1) ? dyDown : 0.5f * (dyDown + dyUp);

            gx[y][x] = gxd;
            gy[y][x] = gyd;

            float m2 = gxd * gxd + gyd * gyd;
            mag[y][x] = std::sqrt(m2);
            ori[y][x] = std::atan2(gyd, gxd);  // [-PI, PI]
        }
    }
}

std::vector<std::vector<std::vector<float>>> computeFHOG(
    const std::vector<std::vector<float>>& grayImage,
    const FHOGConfig& config)
{
    int binSize = config.binSize;
    int nOrients = config.nOrients;
    float clipVal = config.clip;

    int h = static_cast<int>(grayImage.size());
    int w = h > 0 ? static_cast<int>(grayImage[0].size()) : 0;
    if (h < binSize + 1 || w < binSize + 1) {
        qWarning() << "[FHOG] 图像太小: w=" << w << "h=" << h << "< binSize+1=" << binSize + 1;
        return {};
    }

    int hb = h / binSize;
    int wb = w / binSize;
    int nb = hb * wb;
    if (hb < 1 || wb < 1) {
        qWarning() << "[FHOG] hb/wb 为 0: hb=" << hb << "wb=" << wb;
        return {};
    }

    int nFullOrients = nOrients * 2;

    qDebug() << "[FHOG] 输入=" << w << "x" << h
             << "cellGrid=" << wb << "x" << hb << "(nb=" << nb << ")"
             << "bins=" << nOrients << "/" << nFullOrients;

    // 1. 计算梯度
    std::vector<std::vector<float>> gx, gy, mag, ori;
    computeGradients(grayImage, gx, gy, mag, ori);
    qDebug() << "[FHOG] 梯度计算完成";

    // 2. 构建梯度直方图
    std::vector<float> R1(nb * nFullOrients, 0.0f);
    std::vector<float> R2(nb * nOrients, 0.0f);

    for (int by = 0; by < hb; ++by) {
        for (int bx = 0; bx < wb; ++bx) {
            int cellIdx = by * wb + bx;
            // 遍历cell内的像素
            for (int py = 0; py < binSize; ++py) {
                int y = by * binSize + py;
                for (int px = 0; px < binSize; ++px) {
                    int x = bx * binSize + px;
                    float m = mag[y][x];
                    if (m < 1e-6f) continue;

                    float o = ori[y][x];  // [-PI, PI]
                    if (o < 0) o += 2.0f * PI_F;  // 映射到 [0, 2PI)

                    // Contrast-sensitive: 线性插值到相邻bin
                    float obin = o / (2.0f * PI_F) * nFullOrients;
                    int o0 = static_cast<int>(obin);
                    float od = obin - o0;
                    int o1 = (o0 + 1) % nFullOrients;

                    R1[cellIdx + o0 * nb] += m * (1.0f - od);
                    R1[cellIdx + o1 * nb] += m * od;

                    // Contrast-insensitive: 方向取模 PI 后合并
                    float oi = (o < PI_F) ? o : o - PI_F;
                    float oibin = oi / PI_F * nOrients;
                    int oi0 = static_cast<int>(oibin);
                    float oid = oibin - oi0;
                    int oi1 = (oi0 + 1) % nOrients;

                    R2[cellIdx + oi0 * nb] += m * (1.0f - oid);
                    R2[cellIdx + oi1 * nb] += m * oid;
                }
            }
        }
    }

    // 3. 2x2 block normalization
    // block布局：(wb+1) x (hb+1) 个block节点，其中 block(bx, by) (0<=bx<wb, 0<=by<hb) 对应 cell(bx, by) 左上方交点
    // 每个block节点 N[bi][bj] = 当前cell的 R2 平方和（若bi>=wb或bj>=hb则为padding，不贡献sum）
    const int blockCols = wb + 1;  // x方向block节点数
    const int blockRows = hb + 1;  // y方向block节点数
    auto blockIdx = [&](int bi, int bj) -> int { return bj + bi * blockRows; };

    std::vector<float> N(blockCols * blockRows, 0.0f);
    const float eps = 1e-4f;

    for (int o = 0; o < nOrients; ++o) {
        for (int bx = 0; bx < wb; ++bx) {
            for (int by = 0; by < hb; ++by) {
                // R2 的布局: [orient][cell]，cell以 (by, bx) 为行优先索引
                float v = R2[o * nb + by * wb + bx];
                N[blockIdx(bx, by)] += v * v;
            }
        }
    }

    // 边界复制（padding）：将边界节点的值复制到外侧节点，以便边缘cell仍能做2x2归一化
    // - 最右一列 (bi == wb) 复制自 bi == wb-1
    // - 最下一行 (bj == hb) 复制自 bj == hb-1
    // - 右下角交点 (wb, hb) 由行/列复制自然覆盖
    for (int bj = 0; bj < blockRows; ++bj) {
        N[blockIdx(wb, bj)] = N[blockIdx(wb - 1, bj)];
    }
    for (int bi = 0; bi < blockCols; ++bi) {
        N[blockIdx(bi, hb)] = N[blockIdx(bi, hb - 1)];
    }

    // 归一化因子 = 1/sqrt(sum of 4 surrounding block nodes)
    // 对于每个 cell(bx, by)，周围4个block节点为: (bx,by), (bx+1,by), (bx,by+1), (bx+1,by+1)
    std::vector<float> norm(nb, 0.0f);  // 每个cell一个归一化因子
    for (int bx = 0; bx < wb; ++bx) {
        for (int by = 0; by < hb; ++by) {
            float s = N[blockIdx(bx, by)] + N[blockIdx(bx + 1, by)]
                    + N[blockIdx(bx, by + 1)] + N[blockIdx(bx + 1, by + 1)] + eps;
            norm[by * wb + bx] = 1.0f / std::sqrt(s);
        }
    }

    // 4. 构建 FHOG 输出 (31 channels per cell)
    // Channels 0-17:  contrast-sensitive (sum across 4 normalizations)
    // Channels 18-26: contrast-insensitive (sum across 4 normalizations)
    // Channels 27-30: texture channels (sum across orientations)
    std::vector<std::vector<std::vector<float>>> result(
        hb, std::vector<std::vector<float>>(wb, std::vector<float>(31, 0.0f)));

    const float r = 0.2357f;  // texture channel weight

    for (int by = 0; by < hb; ++by) {
        for (int bx = 0; bx < wb; ++bx) {
            // 简化：使用当前cell的单一归一化因子代替4节点插值；边缘稳定且避免越界
            float n0 = norm[by * wb + bx];
            float n[4] = { n0, n0, n0, n0 };

            int cellIdx = by * wb + bx;

            // Channels 0-17: contrast-sensitive (type=1: sum across norms)
            for (int o = 0; o < nFullOrients; ++o) {
                float val = 0.0f;
                for (int b = 0; b < 4; ++b) {
                    float t = R1[o * nb + cellIdx] * n[b];
                    if (t > clipVal) t = clipVal;
                    val += t * 0.5f;
                }
                result[by][bx][o] = val;
            }

            // Channels 18-26: contrast-insensitive (type=1: sum across norms)
            for (int o = 0; o < nOrients; ++o) {
                float val = 0.0f;
                for (int b = 0; b < 4; ++b) {
                    float t = R2[o * nb + cellIdx] * n[b];
                    if (t > clipVal) t = clipVal;
                    val += t * 0.5f;
                }
                result[by][bx][nFullOrients + o] = val;
            }

            // Channels 27-30: texture (type=2: sum across orients)
            for (int b = 0; b < 4; ++b) {
                float tSum = 0.0f;
                for (int o = 0; o < nFullOrients; ++o) {
                    float t = R1[o * nb + cellIdx] * n[b];
                    if (t > clipVal) t = clipVal;
                    tSum += t * r;
                }
                result[by][bx][27 + b] = tSum;
            }
        }
    }

    return result;
}

std::vector<std::vector<float>> flattenFHOG(
    const std::vector<std::vector<std::vector<float>>>& fhog)
{
    int rows = static_cast<int>(fhog.size());
    if (rows == 0) return {};
    int cols = static_cast<int>(fhog[0].size());
    if (cols == 0) return {};
    int chans = static_cast<int>(fhog[0][0].size());

    std::vector<std::vector<float>> result(rows * cols, std::vector<float>(chans, 0.0f));
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
            for (int ch = 0; ch < chans; ++ch)
                result[r * cols + c][ch] = fhog[r][c][ch];

    return result;
}
