#pragma once
#include <string>

namespace rust {

// Maps % of surface rusted to an ASTM D610 style grade (10 = best, 0 = worst).
// Thresholds follow the published D610 scale; verify against your copy of the standard.
int astmD610Grade(double percent);
std::string describeGrade(int grade);

}  // namespace rust
