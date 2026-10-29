#pragma once
#include <opencv2/core.hpp>
#include <string>

namespace rust {

struct Params {
    // HSV ranges (OpenCV hue is 0..179). Rust is orange/brown, sometimes red.
    int hLow = 0, hHigh = 25;
    int hLow2 = 170, hHigh2 = 179;
    int sMin = 70, vMin = 40, vMax = 245;
    // Texture gate: local std-dev of gray must be >= texMin (0 disables).
    int texWin = 7;
    double texMin = 3.0;
    // Morphology / cleanup
    int openK = 3, closeK = 7;
    int minArea = 30;
};

class RustSegmenter {
public:
    explicit RustSegmenter(Params p = {});
    // Returns CV_8U mask with 255 on rust pixels. Const and stateless => thread-safe.
    cv::Mat segment(const cv::Mat& bgr) const;
    const Params& params() const { return p_; }

private:
    Params p_;
    cv::Mat openKernel_, closeKernel_;
};

double rustPercent(const cv::Mat& mask);
cv::Mat drawOverlay(const cv::Mat& bgr, const cv::Mat& mask, const std::string& text);

}  // namespace rust
