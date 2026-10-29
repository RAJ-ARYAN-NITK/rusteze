// RustScan C++ HTTP Server & Live Dashboard
//
// Pure C++ backend running an embedded web server (cpp-httplib) that interfaces
// directly with OpenCV segmentation, ASTM D610 grading, and optional Ollama VLM.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "httplib.h"
#include "nlohmann/json.hpp"

#include "rust/base64.hpp"
#include "rust/ollama.hpp"
#include "rust/report.hpp"
#include "rust/segmenter.hpp"
#include "rust/severity.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

static std::string readFile(const fs::path& path) {
    std::ifstream f(path, std::ios::in | std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Generate realistic synthetic metal images on the fly if test files are missing
static cv::Mat generateSyntheticSample(const std::string& name) {
    const int W = 640, H = 480;
    cv::Mat img(H, W, CV_8UC3);

    if (name == "clean_pipe") {
        // Smooth brushed metal / steel cylinder
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                double g = 135.0 + 35.0 * std::sin(x * 0.015) + (std::rand() % 7 - 3);
                g = std::max(0.0, std::min(255.0, g));
                img.at<cv::Vec3b>(y, x) = cv::Vec3b(static_cast<uchar>(g * 0.98),
                                                    static_cast<uchar>(g * 0.95),
                                                    static_cast<uchar>(g * 0.93));
            }
        }
    } else if (name == "pitted_plate") {
        // Brushed steel background with scattered rust pits
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                double g = 145.0 + (std::rand() % 12 - 6);
                img.at<cv::Vec3b>(y, x) = cv::Vec3b(static_cast<uchar>(g), static_cast<uchar>(g), static_cast<uchar>(g));
            }
        }
        // Scatter ~45 small pitted rust spots
        std::srand(42);
        for (int i = 0; i < 45; ++i) {
            int cx = 40 + std::rand() % (W - 80);
            int cy = 40 + std::rand() % (H - 80);
            int rad = 4 + std::rand() % 12;
            for (int dy = -rad; dy <= rad; ++dy) {
                for (int dx = -rad; dx <= rad; ++dx) {
                    if (dx*dx + dy*dy <= rad*rad) {
                        int px = cx + dx, py = cy + dy;
                        if (px >= 0 && px < W && py >= 0 && py < H) {
                            int noise = (std::rand() % 25 - 12);
                            // BGR for dark orange-brown rust with texture
                            uchar b = static_cast<uchar>(std::clamp(18 + noise, 0, 255));
                            uchar g = static_cast<uchar>(std::clamp(65 + noise, 0, 255));
                            uchar r = static_cast<uchar>(std::clamp(165 + noise, 0, 255));
                            img.at<cv::Vec3b>(py, px) = cv::Vec3b(b, g, r);
                        }
                    }
                }
            }
        }
    } else if (name == "severe_flange") {
        // Heavy concentrated flaking corrosion patch
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                img.at<cv::Vec3b>(y, x) = cv::Vec3b(130, 130, 130);
            }
        }
        std::srand(101);
        for (int y = 60; y < H - 80; ++y) {
            for (int x = 80; x < W - 100; ++x) {
                double dist = std::hypot((x - 300) / 1.6, (y - 240) / 1.0);
                if (dist < 130 + (std::rand() % 30 - 15)) {
                    int noise = (std::rand() % 40 - 20);
                    uchar b = static_cast<uchar>(std::clamp(22 + noise, 0, 255));
                    uchar g = static_cast<uchar>(std::clamp(75 + noise, 0, 255));
                    uchar r = static_cast<uchar>(std::clamp(185 + noise, 0, 255));
                    img.at<cv::Vec3b>(y, x) = cv::Vec3b(b, g, r);
                }
            }
        }
    } else {
        // structural beam: mottled corrosion along edges
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                bool isFlange = (y < 90 || y > H - 90);
                if (isFlange && (std::rand() % 100 < 60)) {
                    int noise = (std::rand() % 30 - 15);
                    img.at<cv::Vec3b>(y, x) = cv::Vec3b(static_cast<uchar>(std::clamp(25 + noise, 0, 255)),
                                                        static_cast<uchar>(std::clamp(80 + noise, 0, 255)),
                                                        static_cast<uchar>(std::clamp(175 + noise, 0, 255)));
                } else {
                    int bg = 120 + (std::rand() % 10 - 5);
                    img.at<cv::Vec3b>(y, x) = cv::Vec3b(bg, bg, bg);
                }
            }
        }
    }
    return img;
}

int main(int argc, char** argv) {
    int port = 8765;
    std::string host = "0.0.0.0";
    std::string webDir = "web";
    std::string dataDir = "data";
    std::string ollamaHost = "127.0.0.1";
    int ollamaPort = 11434;
    std::string ollamaModel = "";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) port = std::stoi(argv[++i]);
        else if (arg == "--host" && i + 1 < argc) host = argv[++i];
        else if (arg == "--web" && i + 1 < argc) webDir = argv[++i];
        else if (arg == "--data" && i + 1 < argc) dataDir = argv[++i];
        else if (arg == "--ollama-host" && i + 1 < argc) ollamaHost = argv[++i];
        else if (arg == "--ollama-port" && i + 1 < argc) ollamaPort = std::stoi(argv[++i]);
        else if (arg == "--ollama-model" && i + 1 < argc) ollamaModel = argv[++i];
        else if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: rust_server [--port 8765] [--host 0.0.0.0] [--web web] [--data data]\n"
                      << "                   [--ollama-model moondream] [--ollama-host 127.0.0.1] [--ollama-port 11434]\n";
            return 0;
        }
    }

    httplib::Server svr;

    // CORS headers for local API integration
    svr.set_default_headers({
        {"Access-Control-Allow-Origin", "*"},
        {"Access-Control-Allow-Methods", "GET, POST, OPTIONS"},
        {"Access-Control-Allow-Headers", "Content-Type, Authorization"}
    });

    // 1. Health Endpoint
    svr.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
        json j = {{"status", "ok"}, {"service", "rust_server"}, {"version", "1.0.0"}};
        res.set_content(j.dump(), "application/json");
    });

    // 2. Preset Samples Endpoint
    svr.Get(R"(/api/sample/(.*))", [&](const httplib::Request& req, httplib::Response& res) {
        std::string name = req.matches[1];
        cv::Mat sampleImg;

        // Try reading from data/test/images/
        fs::path p1 = fs::path(dataDir) / "test" / "images" / (name + ".jpg");
        fs::path p2 = fs::path(dataDir) / "test" / "images" / (name + ".png");
        if (fs::exists(p1)) sampleImg = cv::imread(p1.string(), cv::IMREAD_COLOR);
        else if (fs::exists(p2)) sampleImg = cv::imread(p2.string(), cv::IMREAD_COLOR);

        if (sampleImg.empty()) {
            // Synthesize realistic sample
            sampleImg = generateSyntheticSample(name);
        }

        std::vector<uchar> buf;
        cv::imencode(".jpg", sampleImg, buf, {cv::IMWRITE_JPEG_QUALITY, 90});
        res.set_content(reinterpret_cast<const char*>(buf.data()), buf.size(), "image/jpeg");
    });

    // 3. Inspection API
    svr.Post("/api/inspect", [&](const httplib::Request& req, httplib::Response& res) {
        auto t0 = Clock::now();

        std::string imgData;
        rust::Params p;

        // Check if multipart form
        if (req.is_multipart_form_data()) {
            if (req.has_file("image")) {
                imgData = req.get_file_value("image").content;
            } else if (req.has_file("file")) {
                imgData = req.get_file_value("file").content;
            }
            if (req.has_param("texMin")) p.texMin = std::stod(req.get_param_value("texMin"));
            if (req.has_param("minArea")) p.minArea = std::stoi(req.get_param_value("minArea"));
            if (req.has_param("hHigh")) p.hHigh = std::stoi(req.get_param_value("hHigh"));
            if (req.has_param("hLow")) p.hLow = std::stoi(req.get_param_value("hLow"));
        } else if (!req.body.empty()) {
            imgData = req.body;
        }

        if (imgData.empty()) {
            res.status = 400;
            res.set_content(json({{"error", "No image data provided in request"}}).dump(), "application/json");
            return;
        }

        std::vector<uchar> rawBuf(imgData.begin(), imgData.end());
        cv::Mat bgr = cv::imdecode(rawBuf, cv::IMREAD_COLOR);
        if (bgr.empty()) {
            res.status = 400;
            res.set_content(json({{"error", "Could not decode image buffer"}}).dump(), "application/json");
            return;
        }

        // Limit dimensions for fast real-time response if extremely huge
        if (std::max(bgr.cols, bgr.rows) > 1920) {
            double scale = 1920.0 / std::max(bgr.cols, bgr.rows);
            cv::resize(bgr, bgr, {}, scale, scale, cv::INTER_AREA);
        }

        // Run C++ Segmentation & Severity Assessment
        rust::RustSegmenter seg(p);
        cv::Mat mask = seg.segment(bgr);
        rust::Assessment as = rust::assess(bgr, mask);
        cv::Mat overlay = rust::drawOverlay(bgr, mask, "");

        auto t1 = Clock::now();
        double elapsedMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

        // Encode outputs to base64
        std::vector<uchar> origJpg, overlayJpg, maskPng;
        cv::imencode(".jpg", bgr, origJpg, {cv::IMWRITE_JPEG_QUALITY, 85});
        cv::imencode(".jpg", overlay, overlayJpg, {cv::IMWRITE_JPEG_QUALITY, 85});
        cv::imencode(".png", mask, maskPng);

        json j;
        j["ok"] = true;
        j["latencyMs"] = elapsedMs;
        j["width"] = bgr.cols;
        j["height"] = bgr.rows;
        j["assessment"] = {
            {"percent", as.percent},
            {"grade", as.grade},
            {"gradeDescription", rust::describeGrade(as.grade)},
            {"regions", as.regions},
            {"largestShare", as.largestShare},
            {"color", as.color},
            {"pattern", as.pattern},
            {"summary", as.summary}
        };
        j["images"] = {
            {"original", "data:image/jpeg;base64," + rust::base64Encode(origJpg)},
            {"overlay", "data:image/jpeg;base64," + rust::base64Encode(overlayJpg)},
            {"mask", "data:image/png;base64," + rust::base64Encode(maskPng)}
        };

        // Optional Ollama evaluation if model is active
        std::string reqModel = ollamaModel;
        if (req.has_param("ollamaModel")) reqModel = req.get_param_value("ollamaModel");

        if (!reqModel.empty()) {
            rust::OllamaClient cli(ollamaHost, ollamaPort, reqModel);
            auto vlmRes = cli.describe(bgr, rust::kRustPrompt);
            if (vlmRes.ok) {
                int verdict = rust::parseYesNo(vlmRes.text);
                j["vlm"] = {
                    {"model", reqModel},
                    {"text", vlmRes.text},
                    {"verdict", verdict == 1 ? "rust" : (verdict == 0 ? "no rust" : "uncertain")},
                    {"latencyMs", vlmRes.ms}
                };
            } else {
                j["vlm"] = {{"model", reqModel}, {"error", vlmRes.error}};
            }
        }

        res.set_content(j.dump(), "application/json");
    });

    // 4. Serve Frontend Dashboard
    auto serveDashboard = [&](const httplib::Request&, httplib::Response& res) {
        fs::path htmlPath = fs::path(webDir) / "index.html";
        std::string html;
        if (fs::exists(htmlPath)) {
            html = readFile(htmlPath);
        }
        if (html.empty()) {
            html = "<!doctype html><h1>RustScan Server Active</h1><p>web/index.html not found on disk.</p>";
        }
        res.set_content(html, "text/html");
    };

    svr.Get("/", serveDashboard);
    svr.Get("/index.html", serveDashboard);

    std::cout << "\n=======================================================\n";
    std::cout << " RustScan C++ HTTP Server & Live Dashboard\n";
    std::cout << "=======================================================\n";
    std::cout << " Listening on: http://" << (host == "0.0.0.0" ? "localhost" : host) << ":" << port << "\n";
    std::cout << " API Inspect:  POST http://localhost:" << port << "/api/inspect\n";
    std::cout << " Web UI:       http://localhost:" << port << "\n";
    if (!ollamaModel.empty()) {
        std::cout << " Ollama Model: " << ollamaModel << " at " << ollamaHost << ":" << ollamaPort << "\n";
    }
    std::cout << "=======================================================\n\n";

    if (!svr.listen(host, port)) {
        std::cerr << "Failed to start HTTP server on " << host << ":" << port << "\n";
        return 1;
    }
    return 0;
}
