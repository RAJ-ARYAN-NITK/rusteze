#include "rust/severity.hpp"

namespace rust {

int astmD610Grade(double p) {
    if (p <= 0.01) return 10;
    if (p <= 0.03) return 9;
    if (p <= 0.1)  return 8;
    if (p <= 0.3)  return 7;
    if (p <= 1.0)  return 6;
    if (p <= 3.0)  return 5;
    if (p <= 10.0) return 4;
    if (p <= 16.0) return 3;
    if (p <= 33.0) return 2;
    if (p <= 50.0) return 1;
    return 0;
}

std::string describeGrade(int g) {
    if (g >= 9) return "negligible";
    if (g >= 7) return "light";
    if (g >= 5) return "moderate";
    if (g >= 3) return "heavy";
    return "severe";
}

}  // namespace rust
