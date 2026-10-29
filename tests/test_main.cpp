#include <cstdio>
#include <cstdlib>
#include <opencv2/imgproc.hpp>
#include <opencv2/core.hpp>
#include "rust/metrics.hpp"
#include "rust/segmenter.hpp"
#include "rust/severity.hpp"
#include "rust/report.hpp"
#include "rust/ollama.hpp"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

int main() {
    // Severity boundaries
    CHECK(rust::astmD610Grade(0.0) == 10);
    CHECK(rust::astmD610Grade(0.01) == 10);
    CHECK(rust::astmD610Grade(0.02) == 9);
    CHECK(rust::astmD610Grade(2.0) == 5);
    CHECK(rust::astmD610Grade(12.0) == 3);
    CHECK(rust::astmD610Grade(60.0) == 0);

    // Synthetic image: grey background with a 100x100 rust-coloured square (BGR for orange-brown)
    cv::Mat img(400, 400, CV_8UC3, cv::Scalar(140, 140, 140));
    cv::rectangle(img, {100, 100}, {199, 199}, cv::Scalar(30, 90, 180), cv::FILLED);
    rust::Params p; p.texMin = 0;  // flat synthetic patch has no texture
    rust::RustSegmenter seg(p);
    cv::Mat mask = seg.segment(img);
    double pct = rust::rustPercent(mask);
    CHECK(pct > 5.5 && pct < 6.5);  // 10000 / 160000 = 6.25%

    rust::Assessment as = rust::assess(img, mask);
    CHECK(as.regions == 1 && as.largestShare > 0.99 && as.pattern == "concentrated patch");
    CHECK(rust::assess(img, cv::Mat::zeros(400, 400, CV_8U)).regions == 0);

    CHECK(rust::parseYesNo("YES. Orange flaky rust.") == 1);
    CHECK(rust::parseYesNo("  no, it is painted") == 0);
    CHECK(rust::parseYesNo("None visible") == -1);
    CHECK(rust::parseYesNo("") == -1);

    // Metrics: perfect prediction
    rust::Confusion c;
    cv::Mat gt = cv::Mat::zeros(400, 400, CV_8U);
    cv::rectangle(gt, {100, 100}, {199, 199}, 255, cv::FILLED);
    double iou = c.add(mask, gt);
    CHECK(iou > 0.95);
    CHECK(c.precision() > 0.95 && c.recall() > 0.95);

    // Metrics: empty prediction vs non-empty ground truth
    rust::Confusion c2;
    c2.add(cv::Mat::zeros(400, 400, CV_8U), gt);
    CHECK(c2.recall() == 0.0);

    std::printf(failures ? "%d test(s) failed\n" : "all tests passed\n", failures);
    return failures ? 1 : 0;
}
