// Batch rust analysis: multithreaded segmentation + ASTM D610 grading + optional evaluation/tuning.
//
// Usage:
//   rust_batch --images DIR [--gt DIR] [--threads N] [--csv out.csv] [--overlays DIR]
//              [--max-width W] [--tune] [--bench] [--report report.html]
//              [--ollama-model NAME] [--ollama-host H] [--ollama-port P] [--ollama-timeout SEC]
// Ground-truth masks are looked up as DIR/<image-stem>.png (white = rust).
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "rust/metrics.hpp"
#include "rust/ollama.hpp"
#include "rust/parallel.hpp"
#include "rust/report.hpp"
#include "rust/segmenter.hpp"
#include "rust/severity.hpp"

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
using rust::Params;

struct Sample { std::string name; cv::Mat img, gt; };

static double ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

static std::vector<Sample> loadSamples(const fs::path& imgDir, const fs::path& gtDir, int maxW) {
    std::vector<fs::path> files;
    for (auto& e : fs::directory_iterator(imgDir)) {
        auto ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp") files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    std::vector<Sample> out;
    for (auto& f : files) {
        Sample s;
        s.name = f.stem().string();
        s.img = cv::imread(f.string(), cv::IMREAD_COLOR);
        if (s.img.empty()) { std::cerr << "skip unreadable: " << f << "\n"; continue; }
        if (maxW > 0 && s.img.cols > maxW)
            cv::resize(s.img, s.img, {maxW, s.img.rows * maxW / s.img.cols}, 0, 0, cv::INTER_AREA);
        if (!gtDir.empty()) {
            cv::Mat g = cv::imread((gtDir / (s.name + ".png")).string(), cv::IMREAD_GRAYSCALE);
            if (!g.empty()) {
                if (g.size() != s.img.size()) cv::resize(g, g, s.img.size(), 0, 0, cv::INTER_NEAREST);
                cv::threshold(g, s.gt, 127, 255, cv::THRESH_BINARY);
            }
        }
        out.push_back(std::move(s));
    }
    return out;
}

struct Eval { rust::Confusion total; double meanIoU = 0; int n = 0; };

static Eval evaluate(const std::vector<Sample>& set, const Params& p, unsigned threads) {
    rust::RustSegmenter seg(p);
    std::vector<rust::Confusion> per(set.size());
    std::vector<double> iou(set.size(), -1.0);
    rust::parallelFor(set.size(), threads, [&](size_t i) {
        if (set[i].gt.empty()) return;
        iou[i] = per[i].add(seg.segment(set[i].img), set[i].gt);
    });
    Eval e;
    for (size_t i = 0; i < set.size(); ++i) {
        if (iou[i] < 0) continue;
        e.total.tp += per[i].tp; e.total.fp += per[i].fp;
        e.total.fn += per[i].fn; e.total.tn += per[i].tn;
        e.meanIoU += iou[i]; ++e.n;
    }
    if (e.n) e.meanIoU /= e.n;
    return e;
}

static Params tune(const std::vector<Sample>& set, unsigned threads) {
    Params best; double bestIoU = -1;
    for (int hHigh : {18, 25, 32})
        for (int sMin : {40, 70, 100})
            for (double texMin : {0.0, 3.0, 6.0})
                for (int closeK : {5, 9}) {
                    Params p; p.hHigh = hHigh; p.sMin = sMin; p.texMin = texMin; p.closeK = closeK;
                    Eval e = evaluate(set, p, threads);
                    if (e.meanIoU > bestIoU) { bestIoU = e.meanIoU; best = p; }
                }
    std::printf("[tune] best mean IoU %.3f  hHigh=%d sMin=%d texMin=%.1f closeK=%d\n",
                bestIoU, best.hHigh, best.sMin, best.texMin, best.closeK);
    return best;
}

int main(int argc, char** argv) {
    std::string imgDir, gtDir, csvPath, overlayDir, reportPath;
    std::string ollamaModel, ollamaHost = "127.0.0.1"; int ollamaPort = 11434; double ollamaTimeout = 120;
    unsigned threads = std::max(1u, std::thread::hardware_concurrency());
    int maxW = 0; bool doTune = false, bench = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--images") imgDir = next();
        else if (a == "--gt") gtDir = next();
        else if (a == "--csv") csvPath = next();
        else if (a == "--overlays") overlayDir = next();
        else if (a == "--report") reportPath = next();
        else if (a == "--ollama-model") ollamaModel = next();
        else if (a == "--ollama-host") ollamaHost = next();
        else if (a == "--ollama-port") ollamaPort = std::stoi(next());
        else if (a == "--ollama-timeout") ollamaTimeout = std::stod(next());
        else if (a == "--threads") threads = std::stoul(next());
        else if (a == "--max-width") maxW = std::stoi(next());
        else if (a == "--tune") doTune = true;
        else if (a == "--bench") bench = true;
    }
    if (imgDir.empty()) { std::cerr << "usage: rust_batch --images DIR [--gt DIR] [--threads N] [--csv f] [--overlays DIR] [--max-width W] [--tune] [--bench]\n"; return 1; }

    auto samples = loadSamples(imgDir, gtDir, maxW);
    std::printf("loaded %zu images, %u threads\n", samples.size(), threads);
    if (samples.empty()) return 1;

    Params params;
    if (doTune) params = tune(samples, threads);  // NOTE: tune on a TRAIN split, report on a TEST split.
    rust::RustSegmenter seg(params);

    // ---- main run ----
    struct Row { double pct = 0, tMs = 0; int grade = 0; };
    std::vector<Row> rows(samples.size());
    std::vector<cv::Mat> masks(samples.size());
    auto t0 = Clock::now();
    rust::parallelFor(samples.size(), threads, [&](size_t i) {
        auto a = Clock::now();
        masks[i] = seg.segment(samples[i].img);
        rows[i].tMs = ms(a, Clock::now());
        rows[i].pct = rust::rustPercent(masks[i]);
        rows[i].grade = rust::astmD610Grade(rows[i].pct);
    });
    double wall = ms(t0, Clock::now());

    // ---- optional: ask a local vision-language model (Ollama) about each image ----
    std::vector<std::string> vlmText(samples.size());
    std::vector<int> vlmVerdict(samples.size(), -1);
    if (!ollamaModel.empty()) {
        rust::OllamaClient ol(ollamaHost, ollamaPort, ollamaModel, ollamaTimeout);
        std::string err;
        if (!ol.ready(&err)) {
            std::cerr << "[ollama] disabled: " << err << "\n";
        } else {
            int asked = 0, agree = 0;
            for (size_t i = 0; i < samples.size(); ++i) {   // sequential: a local VLM serves one request at a time
                auto r = ol.describe(samples[i].img, rust::kRustPrompt);
                if (!r.ok) { vlmText[i] = "[Ollama error: " + r.error + "]"; std::printf("[%zu/%zu] %s: ollama error: %s\n", i + 1, samples.size(), samples[i].name.c_str(), r.error.c_str()); continue; }
                vlmText[i] = r.text;
                vlmVerdict[i] = rust::parseYesNo(r.text);
                bool detector = rows[i].pct > 1.0;   // >1% rust area counts as "rust" for the comparison
                if (vlmVerdict[i] >= 0) { ++asked; agree += ((vlmVerdict[i] == 1) == detector); }
                std::printf("[%zu/%zu] %s: %.1fs | VLM=%s | detector=%s (%.2f%%)\n", i + 1, samples.size(), samples[i].name.c_str(),
                            r.ms / 1000.0, vlmVerdict[i] == 1 ? "rust" : vlmVerdict[i] == 0 ? "no rust" : "?", detector ? "rust" : "no rust", rows[i].pct);
            }
            if (asked) std::printf("detector vs %s agreement: %d/%d (%.0f%%) on images with a clear YES/NO\n", ollamaModel.c_str(), agree, asked, 100.0 * agree / asked);
        }
    }

    if (!csvPath.empty()) {
        std::ofstream csv(csvPath);
        csv << "image,rust_percent,astm_d610_grade,severity,time_ms,vlm_verdict\n";
        for (size_t i = 0; i < samples.size(); ++i)
            csv << samples[i].name << ',' << rows[i].pct << ',' << rows[i].grade << ','
                << rust::describeGrade(rows[i].grade) << ',' << rows[i].tMs << ',' << vlmVerdict[i] << '\n';
    }
    if (!overlayDir.empty()) {
        fs::create_directories(overlayDir);
        for (size_t i = 0; i < samples.size(); ++i) {
            char buf[96];
            std::snprintf(buf, sizeof buf, "Rust %.2f%% | D610 grade %d (%s)", rows[i].pct, rows[i].grade,
                          rust::describeGrade(rows[i].grade).c_str());
            cv::imwrite((fs::path(overlayDir) / (samples[i].name + ".jpg")).string(),
                        rust::drawOverlay(samples[i].img, masks[i], buf));
        }
    }

    if (!reportPath.empty()) {
        std::vector<rust::ReportItem> items(samples.size());
        rust::parallelFor(samples.size(), threads, [&](size_t i) {
            auto& it = items[i];
            it.name = samples[i].name;
            it.a = rust::assess(samples[i].img, masks[i]);
            it.vlmText = vlmText[i]; it.vlmRust = vlmVerdict[i];
            cv::imencode(".jpg", samples[i].img, it.originalJpg);
            cv::imencode(".jpg", rust::drawOverlay(samples[i].img, masks[i], "Rust " + std::to_string(it.a.percent).substr(0, 5) + "%"), it.overlayJpg);
        });
        std::printf(rust::writeHtmlReport(reportPath, items, ollamaModel) ? "report written: %s\n" : "could not write %s\n", reportPath.c_str());
    }

    double sum = 0; for (auto& r : rows) sum += r.tMs;
    std::printf("wall %.1f ms | %.1f img/s | mean per-image %.2f ms\n", wall, 1000.0 * samples.size() / wall, sum / rows.size());

    Eval e = evaluate(samples, params, threads);
    if (e.n)
        std::printf("pixel metrics over %d images: precision %.3f recall %.3f F1 %.3f IoU %.3f | mean per-image IoU %.3f\n",
                    e.n, e.total.precision(), e.total.recall(), e.total.f1(), e.total.iou(), e.meanIoU);

    if (bench) {
        std::printf("\nthreads | best-of-3 wall (ms) | img/s | speedup\n");
        double base = 0;
        for (unsigned t : {1u, 2u, 4u, 8u}) {
            double best = 1e18;
            for (int rep = 0; rep < 3; ++rep) {
                auto a = Clock::now();
                rust::parallelFor(samples.size(), t, [&](size_t i) { (void)seg.segment(samples[i].img); });
                best = std::min(best, ms(a, Clock::now()));
            }
            if (t == 1) base = best;
            std::printf("%7u | %19.1f | %5.1f | %.2fx\n", t, best, 1000.0 * samples.size() / best, base / best);
        }
    }
    return 0;
}
