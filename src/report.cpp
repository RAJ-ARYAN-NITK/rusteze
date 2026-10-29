#include "rust/report.hpp"
#include "rust/base64.hpp"
#include "rust/segmenter.hpp"
#include "rust/severity.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <opencv2/imgproc.hpp>
#include <iomanip>
#include <sstream>

namespace rust {

Assessment assess(const cv::Mat& bgr, const cv::Mat& mask) {
    Assessment a;
    a.percent = rustPercent(mask);
    a.grade = astmD610Grade(a.percent);

    cv::Mat labels, stats, cent;
    int n = cv::connectedComponentsWithStats(mask, labels, stats, cent, 8, CV_32S);
    a.regions = n - 1;
    long long total = 0, largest = 0;
    for (int i = 1; i < n; ++i) {
        long long ar = stats.at<int>(i, cv::CC_STAT_AREA);
        total += ar; largest = std::max(largest, ar);
    }
    a.largestShare = total ? static_cast<double>(largest) / total : 0.0;

    if (a.regions == 0) {
        a.color = "none"; a.pattern = "none";
    } else {
        cv::Mat hsv; cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);
        cv::Scalar m = cv::mean(hsv, mask);   // m[0]=hue, m[2]=value
        if (m[2] < 90) a.color = "dark brown";
        else if (m[0] < 10) a.color = "reddish-brown";
        else if (m[0] < 18) a.color = "orange-brown";
        else a.color = "yellow-orange";
        if (a.largestShare > 0.6) a.pattern = "concentrated patch";
        else if (a.regions > 20) a.pattern = "scattered spots (possible pitting)";
        else a.pattern = "several patches";
    }

    std::ostringstream s;
    if (a.regions == 0) s << "No rust detected.";
    else s << describeGrade(a.grade) << " corrosion: " << std::fixed << std::setprecision(2) << a.percent << "% of the surface in "
           << a.regions << " region(s), " << a.pattern << ", " << a.color << " in colour.";
    a.summary = s.str();
    return a;
}

static std::string b64(const std::vector<unsigned char>& d) { return base64Encode(d); }

static std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) { if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;"; else if (c == '&') o += "&amp;"; else o += c; }
    return o;
}

bool writeHtmlReport(const std::string& path, const std::vector<ReportItem>& items, const std::string& vlmModel) {
    std::ofstream f(path);
    if (!f) return false;
    f << "<!doctype html><meta charset=utf-8><title>RustScan report</title><style>"
         "body{font-family:system-ui,sans-serif;margin:24px;background:#111;color:#eee}"
         "h1{margin:0 0 4px}.card{background:#1c1c1c;border-radius:10px;padding:14px;margin:16px 0}"
         ".row{display:flex;gap:12px;flex-wrap:wrap}img{max-width:420px;border-radius:6px}"
         ".g{font-size:28px;font-weight:700}.meta{color:#aaa}table{border-collapse:collapse;margin-top:8px}"
         "td{padding:2px 14px 2px 0}</style><h1>RustScan inspection report</h1>"
         "<div class=meta>" << items.size() << " image(s). Grades follow an ASTM D610-style scale (10 = no rust, 0 = severe). "
         "Condition text is rule-based.</div>";
    for (const auto& it : items) {
        const auto& a = it.a;
        f << "<div class=card><b>" << esc(it.name) << "</b><div class=row>"
          << "<img src='data:image/jpeg;base64," << b64(it.originalJpg) << "'>"
          << "<img src='data:image/jpeg;base64," << b64(it.overlayJpg) << "'>"
          << "<div><div class=g>Grade " << a.grade << "/10</div><div>" << esc(a.summary) << "</div>"
          << "<table><tr><td>Rust area</td><td>" << std::fixed << std::setprecision(2) << a.percent << " %</td></tr>"
          << "<tr><td>Regions</td><td>" << a.regions << "</td></tr>"
          << "<tr><td>Largest region share</td><td>" << static_cast<int>(a.largestShare * 100) << " %</td></tr>"
          << "<tr><td>Colour</td><td>" << a.color << "</td></tr>"
          << "<tr><td>Pattern</td><td>" << a.pattern << "</td></tr></table>";
        if (!it.vlmText.empty()) {
            const char* v = it.vlmRust == 1 ? "rust" : it.vlmRust == 0 ? "no rust" : "no clear verdict";
            f << "<div style='margin-top:10px;max-width:440px'><div class=meta>AI description (" << esc(vlmModel)
              << ", qualitative, unverified) - verdict: " << v << "</div>" << esc(it.vlmText) << "</div>";
        }
        f << "</div></div></div>";
    }
    return true;
}

}  // namespace rust
