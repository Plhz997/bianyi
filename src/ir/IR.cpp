#include "ir/IR.h"

#include <ostream>

namespace by::ir {

void Module::dump(std::ostream &os) const {
  for (const auto &global : globals) {
    os << "global @" << global.name << " : " << global.type;
    if (!global.init.empty()) {
      os << " = " << global.init;
    }
    os << '\n';
  }

  if (!globals.empty() && !functions.empty()) {
    os << '\n';
  }

  for (const auto &function : functions) {
    os << "func @" << function.name << '(';
    for (std::size_t i = 0; i < function.params.size(); ++i) {
      if (i != 0) {
        os << ", ";
      }
      os << function.params[i];
    }
    os << ") -> " << function.returnType << " {\n";

    for (const auto &block : function.blocks) {
      os << block.label << ":\n";
      for (const auto &inst : block.instructions) {
        os << "  " << inst << '\n';
      }
    }

    os << "}\n\n";
  }
}

} // namespace by::ir
