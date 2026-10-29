// Real-time pipeline: capture thread -> N worker threads -> render/writer (main thread).
//
// Usage:
//   rust_video --src VIDEO_OR_CAMERA_INDEX [--workers N] [--width W] [--out out.mp4]
//              [--headless] [--live] [--queue N]
//  --live : drop oldest frames instead of blocking (use for cameras, keeps latency low)
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <map>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include "rust/bounded_queue.hpp"
#include "rust/segmenter.hpp"
#include "rust/severity.hpp"

using Clock = std::chrono::steady_clock;
static double msSince(Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

struct Frame { long long id; cv::Mat img; Clock::time_point t0; };
struct Result { long long id; cv::Mat vis; double pct; int grade; double procMs; Clock::time_point t0; };

static double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, static_cast<size_t>(p * v.size()))];
}

int main(int argc, char** argv) {
    std::string src, outPath;
    unsigned workers = 2; int width = 640; size_t qcap = 8;
    bool headless = false, live = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--src") src = next();
        else if (a == "--workers") workers = std::stoul(next());
        else if (a == "--width") width = std::stoi(next());
        else if (a == "--out") outPath = next();
        else if (a == "--queue") qcap = std::stoul(next());
        else if (a == "--headless") headless = true;
        else if (a == "--live") live = true;
    }
    if (src.empty()) { std::cerr << "usage: rust_video --src FILE|CAM_INDEX [--workers N] [--width W] [--out f] [--headless] [--live]\n"; return 1; }

    cv::VideoCapture cap;
    bool isIndex = std::all_of(src.begin(), src.end(), ::isdigit);
    if (isIndex) cap.open(std::stoi(src)); else cap.open(src);
    if (!cap.isOpened()) { std::cerr << "cannot open source " << src << "\n"; return 1; }
    double srcFps = cap.get(cv::CAP_PROP_FPS); if (srcFps <= 1) srcFps = 25;

    rust::RustSegmenter seg;  // shared, const => safe across workers
    rust::BoundedQueue<Frame> q1(qcap);
    rust::BoundedQueue<Result> q2(qcap);
    std::atomic<bool> stop{false};
    std::atomic<unsigned> activeWorkers{workers};

    // Producer
    std::thread producer([&] {
        long long id = 0; cv::Mat f;
        while (!stop && cap.read(f)) {
            if (width > 0 && f.cols > width)
                cv::resize(f, f, {width, f.rows * width / f.cols}, 0, 0, cv::INTER_AREA);
            if (!q1.push({id++, f.clone(), Clock::now()}, live)) break;
        }
        q1.close();
    });

    // Workers
    std::vector<std::thread> pool;
    for (unsigned w = 0; w < workers; ++w)
        pool.emplace_back([&] {
            while (auto fr = q1.pop()) {
                auto a = Clock::now();
                cv::Mat mask = seg.segment(fr->img);
                double pct = rust::rustPercent(mask);
                int grade = rust::astmD610Grade(pct);
                double procMs = msSince(a);
                char buf[96];
                std::snprintf(buf, sizeof buf, "Rust %.2f%% | D610 grade %d (%s)", pct, grade,
                              rust::describeGrade(grade).c_str());
                cv::Mat vis = rust::drawOverlay(fr->img, mask, buf);
                if (!q2.push({fr->id, std::move(vis), pct, grade, procMs, fr->t0})) break;
            }
            if (--activeWorkers == 0) q2.close();
        });

    // Consumer (render / write). A small reorder buffer restores frame order across workers.
    cv::VideoWriter writer;
    std::vector<double> latency, proc;
    std::map<long long, Result> pending;
    long long nextId = 0; size_t shown = 0;
    auto tStart = Clock::now();

    auto emit = [&](Result& r) {
        latency.push_back(msSince(r.t0));
        proc.push_back(r.procMs);
        if (!outPath.empty()) {
            if (!writer.isOpened()) writer.open(outPath, cv::VideoWriter::fourcc('m', 'p', '4', 'v'), srcFps, r.vis.size());
            writer.write(r.vis);
        }
        if (!headless) {
            cv::imshow("rust_video (q to quit)", r.vis);
            if ((cv::waitKey(1) & 0xFF) == 'q') stop = true;
        }
        ++shown;
    };

    while (auto r = q2.pop()) {
        pending.emplace(r->id, std::move(*r));
        // emit in order; if the buffer grows (frames were dropped), skip the gap
        while (!pending.empty()) {
            auto it = pending.begin();
            if (it->first != nextId && pending.size() <= workers * 2 + 2) break;
            nextId = it->first + 1;
            emit(it->second);
            pending.erase(it);
        }
        if (stop) { q1.close(); q2.close(); }
    }
    for (auto& [id, r] : pending) emit(r);
    producer.join();
    for (auto& t : pool) t.join();

    double wall = msSince(tStart) / 1000.0;
    double meanLat = latency.empty() ? 0 : std::accumulate(latency.begin(), latency.end(), 0.0) / latency.size();
    double meanProc = proc.empty() ? 0 : std::accumulate(proc.begin(), proc.end(), 0.0) / proc.size();
    std::printf("\nframes processed : %zu (dropped: %zu)\n", shown, q1.dropped());
    std::printf("throughput       : %.1f FPS  (%u workers, width %d)\n", shown / wall, workers, width);
    std::printf("process time     : mean %.2f ms/frame\n", meanProc);
    std::printf("end-to-end lat.  : mean %.1f ms | p50 %.1f | p95 %.1f\n", meanLat, percentile(latency, 0.5), percentile(latency, 0.95));
    return 0;
}
