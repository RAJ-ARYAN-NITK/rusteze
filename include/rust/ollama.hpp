#pragma once
#include <opencv2/core.hpp>
#include <string>

namespace rust {

struct OllamaResult {
    bool ok = false;
    std::string text, error;
    double ms = 0;
};

// Minimal client for a local Ollama server (default http://127.0.0.1:11434).
// Uses POST /api/generate with a base64 image, which any vision model (moondream, llava, ...) accepts.
class OllamaClient {
public:
    OllamaClient(std::string host, int port, std::string model, double timeoutSec = 120.0);

    // True if the server answers and the model has been pulled. Otherwise fills *err with a fix-it hint.
    bool ready(std::string* err = nullptr) const;
    OllamaResult describe(const cv::Mat& bgr, const std::string& prompt) const;
    const std::string& model() const { return model_; }

private:
    std::string host_, model_;
    int port_;
    double timeout_;
};

// Reads a leading YES/NO from a model answer. Returns 1, 0, or -1 if the answer doesn't start with either.
int parseYesNo(const std::string& answer);

// Prompt used for rust verification. Deliberately does not mention the detector's numbers,
// so the model's verdict stays independent and can be compared against the detector.
extern const char* const kRustPrompt;

}  // namespace rust
