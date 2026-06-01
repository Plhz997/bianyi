#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace by::ir {

struct Global {
  std::string name;
  std::string type;
  std::string init;
};

struct BasicBlock {
  std::string label;
  std::vector<std::string> instructions;
  bool terminated = false;
};

struct Function {
  std::string name;
  std::string returnType;
  std::vector<std::string> params;
  std::vector<BasicBlock> blocks;
};

struct Module {
  std::vector<Global> globals;
  std::vector<Function> functions;

  void dump(std::ostream &os) const;
};

} // namespace by::ir
