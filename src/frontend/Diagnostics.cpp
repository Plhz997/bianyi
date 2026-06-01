#include "frontend/Diagnostics.h"

#include <sstream>

namespace by {

std::string SourceLocation::str() const {
  std::ostringstream os;
  if (!file.empty()) {
    os << file << ':';
  }
  os << line << ':' << column;
  return os.str();
}

void DiagnosticEngine::error(const SourceLocation &loc,
                             const std::string &message) {
  ++errors_;
  diagnostics_.push_back({DiagnosticLevel::Error, loc, message});
}

void DiagnosticEngine::warning(const SourceLocation &loc,
                               const std::string &message) {
  diagnostics_.push_back({DiagnosticLevel::Warning, loc, message});
}

void DiagnosticEngine::printAll(std::ostream &os) const {
  for (const auto &diag : diagnostics_) {
    os << diag.loc.str() << ": "
       << (diag.level == DiagnosticLevel::Error ? "error" : "warning")
       << ": " << diag.message << '\n';
  }
}

} // namespace by

