#pragma once

#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

namespace by {

struct SourceLocation {
  std::string file;
  std::size_t offset = 0;
  int line = 1;
  int column = 1;

  std::string str() const;
};

enum class DiagnosticLevel {
  Error,
  Warning,
};

struct Diagnostic {
  DiagnosticLevel level;
  SourceLocation loc;
  std::string message;
};

class DiagnosticEngine {
public:
  void error(const SourceLocation &loc, const std::string &message);
  void warning(const SourceLocation &loc, const std::string &message);

  bool hasErrors() const { return errors_ != 0; }
  std::size_t errorCount() const { return errors_; }
  const std::vector<Diagnostic> &diagnostics() const { return diagnostics_; }
  void printAll(std::ostream &os = std::cerr) const;

private:
  std::size_t errors_ = 0;
  std::vector<Diagnostic> diagnostics_;
};

} // namespace by

