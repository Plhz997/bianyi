#pragma once

#include "frontend/AST.h"

#include <iosfwd>

namespace by::backend {

struct RiscVCodeGenOptions {
  bool optimize = false;
};

class RiscVCodeGenerator {
public:
  explicit RiscVCodeGenerator(RiscVCodeGenOptions options = {});

  void generate(const CompUnit &unit, std::ostream &os);

private:
  RiscVCodeGenOptions options_;
};

} // namespace by::backend
