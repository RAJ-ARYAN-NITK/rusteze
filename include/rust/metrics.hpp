#pragma once
#include <opencv2/core.hpp>

namespace rust {

struct Confusion {
    long long tp = 0, fp = 0, fn = 0, tn = 0;

    // Returns this image's IoU while accumulating into the totals.
    double add(const cv::Mat& pred, const cv::Mat& gt) {
        CV_Assert(pred.size() == gt.size());
        cv::Mat inter;
        cv::bitwise_and(pred, gt, inter);
        long long t = cv::countNonZero(inter);
        long long p = cv::countNonZero(pred), g = cv::countNonZero(gt);
        long long total = static_cast<long long>(pred.total());
        tp += t; fp += p - t; fn += g - t; tn += total - p - g + t;
        long long uni = p + g - t;
        return uni == 0 ? 1.0 : static_cast<double>(t) / uni;
    }
    double precision() const { return tp + fp ? double(tp) / (tp + fp) : 0.0; }
    double recall()    const { return tp + fn ? double(tp) / (tp + fn) : 0.0; }
    double iou()       const { return tp + fp + fn ? double(tp) / (tp + fp + fn) : 1.0; }
    double f1()        const { double p = precision(), r = recall(); return p + r ? 2 * p * r / (p + r) : 0.0; }
};

}  // namespace rust
