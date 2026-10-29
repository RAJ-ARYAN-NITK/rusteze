#include "rust/segmenter.hpp"
#include <opencv2/imgproc.hpp>
#include <vector>

namespace rust {

RustSegmenter::RustSegmenter(Params p) : p_(p) {
    openKernel_  = cv::getStructuringElement(cv::MORPH_ELLIPSE, {p_.openK, p_.openK});
    closeKernel_ = cv::getStructuringElement(cv::MORPH_ELLIPSE, {p_.closeK, p_.closeK});
}

cv::Mat RustSegmenter::segment(const cv::Mat& bgr) const {
    CV_Assert(!bgr.empty() && bgr.type() == CV_8UC3);

    // 1) Colour gate in HSV
    cv::Mat hsv, m1, m2, mask;
    cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);
    cv::inRange(hsv, cv::Scalar(p_.hLow,  p_.sMin, p_.vMin), cv::Scalar(p_.hHigh,  255, p_.vMax), m1);
    cv::inRange(hsv, cv::Scalar(p_.hLow2, p_.sMin, p_.vMin), cv::Scalar(p_.hHigh2, 255, p_.vMax), m2);
    cv::bitwise_or(m1, m2, mask);

    // 2) Texture gate: local standard deviation (rust is rough, orange paint is smooth)
    if (p_.texMin > 0.0) {
        cv::Mat gray, g32, mean, sq, var, sd, tex;
        cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
        gray.convertTo(g32, CV_32F);
        const cv::Size w(p_.texWin, p_.texWin);
        cv::boxFilter(g32, mean, CV_32F, w);
        cv::boxFilter(g32.mul(g32), sq, CV_32F, w);
        var = cv::max(sq - mean.mul(mean), 0.0f);
        cv::sqrt(var, sd);
        cv::threshold(sd, tex, p_.texMin, 255, cv::THRESH_BINARY);
        tex.convertTo(tex, CV_8U);
        cv::bitwise_and(mask, tex, mask);
    }

    // 3) Morphological cleanup: remove speckle, then fill small gaps
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, openKernel_);
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, closeKernel_);

    // 4) Drop tiny connected components
    cv::Mat labels, stats, cent;
    const int n = cv::connectedComponentsWithStats(mask, labels, stats, cent, 8, CV_32S);
    std::vector<uchar> keep(n, 0);
    for (int i = 1; i < n; ++i)
        keep[i] = stats.at<int>(i, cv::CC_STAT_AREA) >= p_.minArea ? 255 : 0;
    cv::Mat out(mask.size(), CV_8U);
    for (int y = 0; y < out.rows; ++y) {
        const int* l = labels.ptr<int>(y);
        uchar* o = out.ptr<uchar>(y);
        for (int x = 0; x < out.cols; ++x) o[x] = keep[l[x]];
    }
    return out;
}

double rustPercent(const cv::Mat& mask) {
    return mask.empty() ? 0.0 : 100.0 * cv::countNonZero(mask) / static_cast<double>(mask.total());
}

cv::Mat drawOverlay(const cv::Mat& bgr, const cv::Mat& mask, const std::string& text) {
    cv::Mat vis = bgr.clone(), red(bgr.size(), CV_8UC3, cv::Scalar(0, 0, 255));
    cv::Mat blended;
    cv::addWeighted(bgr, 0.5, red, 0.5, 0, blended);
    blended.copyTo(vis, mask);
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask.clone(), contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    cv::drawContours(vis, contours, -1, cv::Scalar(0, 255, 255), 1);
    if (!text.empty()) {
        cv::rectangle(vis, {0, 0}, {vis.cols, 26}, cv::Scalar(0, 0, 0), cv::FILLED);
        cv::putText(vis, text, {6, 18}, cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1, cv::LINE_AA);
    }
    return vis;
}

}  // namespace rust
