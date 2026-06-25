#pragma once

#include "AST.h"

namespace by {

class ASTOptimizer {
public:
  void optimize(CompUnit &unit);
};

} // namespace by
