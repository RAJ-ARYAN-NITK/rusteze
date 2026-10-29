#pragma once
#include <opencv2/core.hpp>
#include <string>
#include <vector>

namespace rust {

// Rule-based condition assessment (no ML, no cloud): derived from the mask and the rusty pixels' colour.
struct Assessment {
    double percent = 0;
    int grade = 10;
    int regions = 0;
    double largestShare = 0;   // fraction of rust area held by the biggest region (0..1)
    std::string color;         // dark brown / reddish-brown / orange-brown / yellow-orange / none
    std::string pattern;       // concentrated patch / several patches / scattered spots / none
    std::string summary;       // one readable sentence
};

Assessment assess(const cv::Mat& bgr, const cv::Mat& mask);

struct ReportItem {
    std::string name;
    Assessment a;
    std::vector<unsigned char> originalJpg, overlayJpg;
    std::string vlmText;   // optional answer from a local vision-language model (Ollama)
    int vlmRust = -1;      // 1 = model says rust, 0 = no, -1 = unknown / not used
};

// Writes one self-contained HTML page (images embedded as base64): original | annotated | findings.
bool writeHtmlReport(const std::string& path, const std::vector<ReportItem>& items,
                     const std::string& vlmModel = "");

}  // namespace rust
