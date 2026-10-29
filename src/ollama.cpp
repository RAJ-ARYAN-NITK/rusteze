#include "rust/ollama.hpp"
#include "rust/base64.hpp"

#include <chrono>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "httplib.h"
#include "nlohmann/json.hpp"

namespace rust {

using json = nlohmann::json;

const char* const kRustPrompt =
    "You are assisting with a metal corrosion inspection. Start your answer with exactly YES or NO: "
    "is there visible rust or corrosion on metal in this photo? Then, in at most three sentences, describe "
    "the rust colour, whether it looks like light surface rust, flaking or pitting, and anything in the photo "
    "that could be mistaken for rust (paint, wood, dirt). Do not invent measurements.";

OllamaClient::OllamaClient(std::string host, int port, std::string model, double timeoutSec)
    : host_(std::move(host)), model_(std::move(model)), port_(port), timeout_(timeoutSec) {}

bool OllamaClient::ready(std::string* err) const {
    auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
    httplib::Client cli(host_, port_);
    cli.set_connection_timeout(3);
    cli.set_read_timeout(10);
    auto res = cli.Get("/api/tags");
    if (!res)
        return fail("Cannot reach Ollama at " + host_ + ":" + std::to_string(port_) + " (" +
                    httplib::to_string(res.error()) + "). Start it with `ollama serve` or open the Ollama app.");
    if (res->status != 200) return fail("Ollama answered HTTP " + std::to_string(res->status));
    try {
        json j = json::parse(res->body);
        for (const auto& m : j.value("models", json::array())) {
            std::string name = m.value("name", "");
            if (name == model_ || name.rfind(model_ + ":", 0) == 0) return true;
        }
    } catch (const std::exception& e) { return fail(std::string("Bad /api/tags reply: ") + e.what()); }
    return fail("Model '" + model_ + "' is not installed. Run: ollama pull " + model_);
}

OllamaResult OllamaClient::describe(const cv::Mat& bgr, const std::string& prompt) const {
    OllamaResult r;
    auto t0 = std::chrono::steady_clock::now();
    try {
        cv::Mat img = bgr;
        const int longSide = std::max(bgr.cols, bgr.rows);
        if (longSide > 768) {   // smaller images => much faster VLM inference
            double s = 768.0 / longSide;
            cv::resize(bgr, img, {}, s, s, cv::INTER_AREA);
        }
        std::vector<unsigned char> jpg;
        cv::imencode(".jpg", img, jpg, {cv::IMWRITE_JPEG_QUALITY, 85});

        json body = {{"model", model_},
                     {"prompt", prompt},
                     {"images", json::array({base64Encode(jpg)})},
                     {"stream", false},
                     {"options", {{"temperature", 0.2}, {"num_predict", 200}}}};

        httplib::Client cli(host_, port_);
        cli.set_connection_timeout(3);
        cli.set_read_timeout(static_cast<time_t>(timeout_));
        auto res = cli.Post("/api/generate", body.dump(), "application/json");
        if (!res) {
            r.error = "request failed: " + httplib::to_string(res.error());
        } else if (res->status != 200) {
            r.error = "HTTP " + std::to_string(res->status) + ": " + res->body.substr(0, 200);
        } else {
            json j = json::parse(res->body);
            r.text = j.value("response", "");
            r.ok = !r.text.empty();
            if (!r.ok) r.error = "empty response";
        }
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return r;
}

int parseYesNo(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && !std::isalpha(static_cast<unsigned char>(s[i]))) ++i;
    std::string w;
    while (i < s.size() && std::isalpha(static_cast<unsigned char>(s[i]))) w += static_cast<char>(std::toupper(static_cast<unsigned char>(s[i++])));
    if (w == "YES") return 1;
    if (w == "NO") return 0;
    return -1;
}

}  // namespace rust
