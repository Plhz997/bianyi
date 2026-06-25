#include "../../include/backend/RiscVCodeGen.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <cmath>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace by::backend {

namespace {

constexpr int kWordSize = 4;
constexpr int kPtrSize = 8;

uint32_t floatBits(double value) {
  float narrowed = static_cast<float>(value);
  uint32_t bits = 0;
  std::memcpy(&bits, &narrowed, sizeof(bits));
  return bits;
}

struct Symbol {
  enum class Storage {
    Global,
    Local,
    ParamPtr,
  };

  Storage storage = Storage::Local;
  BuiltinType type = BuiltinType::Int;
  bool isConst = false;
  bool isArray = false;
  std::vector<long long> dims;
  long long offset = 0;
  std::string label;
};

struct FunctionInfo {
  BuiltinType returnType = BuiltinType::Int;
  std::vector<BuiltinType> paramTypes;
  std::vector<bool> paramIsArray;
};

struct LoopLabels {
  std::string continueLabel;
  std::string breakLabel;
};

long long alignTo(long long value, long long align) {
  return (value + align - 1) / align * align;
}

std::string sanitizeLabel(const std::string &name) {
  std::string result;
  result.reserve(name.size());
  for (char c : name) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_') {
      result.push_back(c);
    } else {
      result.push_back('_');
    }
  }
  if (result.empty() || (result[0] >= '0' && result[0] <= '9')) {
    result.insert(result.begin(), '_');
  }
  return result;
}

long long evalConstInt(
    const Expr &expr,
    const std::vector<std::unordered_map<std::string, long long>> &constScopes);

double evalConstFloat(
    const Expr &expr,
    const std::vector<std::unordered_map<std::string, long long>> &intScopes,
    const std::vector<std::unordered_map<std::string, double>> &floatScopes);

long long evalConstInt(
    const Expr &expr,
    const std::vector<std::unordered_map<std::string, long long>> &constScopes) {
  if (const auto *integer = dynamic_cast<const IntExpr *>(&expr)) {
    return integer->value;
  }
  if (const auto *floating = dynamic_cast<const FloatExpr *>(&expr)) {
    return static_cast<long long>(floating->value);
  }
  if (const auto *var = dynamic_cast<const VarExpr *>(&expr)) {
    if (!var->indices.empty()) {
      throw std::runtime_error("array element is not a compile-time constant");
    }
    for (auto it = constScopes.rbegin(); it != constScopes.rend(); ++it) {
      auto found = it->find(var->name);
      if (found != it->end()) {
        return found->second;
      }
    }
    throw std::runtime_error("unknown compile-time constant: " + var->name);
  }
  if (const auto *unary = dynamic_cast<const UnaryExpr *>(&expr)) {
    long long value = evalConstInt(*unary->operand, constScopes);
    if (unary->op == "+") {
      return value;
    }
    if (unary->op == "-") {
      return -value;
    }
    if (unary->op == "!") {
      return value == 0 ? 1 : 0;
    }
  }
  if (const auto *binary = dynamic_cast<const BinaryExpr *>(&expr)) {
    long long lhs = evalConstInt(*binary->lhs, constScopes);
    long long rhs = evalConstInt(*binary->rhs, constScopes);
    if (binary->op == "+") return lhs + rhs;
    if (binary->op == "-") return lhs - rhs;
    if (binary->op == "*") return lhs * rhs;
    if (binary->op == "/") return rhs == 0 ? 0 : lhs / rhs;
    if (binary->op == "%") return rhs == 0 ? 0 : lhs % rhs;
    if (binary->op == "==") return lhs == rhs;
    if (binary->op == "!=") return lhs != rhs;
    if (binary->op == "<") return lhs < rhs;
    if (binary->op == "<=") return lhs <= rhs;
    if (binary->op == ">") return lhs > rhs;
    if (binary->op == ">=") return lhs >= rhs;
    if (binary->op == "&&") return (lhs != 0) && (rhs != 0);
    if (binary->op == "||") return (lhs != 0) || (rhs != 0);
  }
  throw std::runtime_error("expression is not a compile-time integer");
}

double evalConstFloat(
    const Expr &expr,
    const std::vector<std::unordered_map<std::string, long long>> &intScopes,
    const std::vector<std::unordered_map<std::string, double>> &floatScopes) {
  if (const auto *floating = dynamic_cast<const FloatExpr *>(&expr)) {
    return floating->value;
  }
  if (const auto *integer = dynamic_cast<const IntExpr *>(&expr)) {
    return static_cast<double>(integer->value);
  }
  if (const auto *var = dynamic_cast<const VarExpr *>(&expr)) {
    if (!var->indices.empty()) {
      throw std::runtime_error("array element is not a compile-time constant");
    }
    for (auto it = floatScopes.rbegin(); it != floatScopes.rend(); ++it) {
      auto found = it->find(var->name);
      if (found != it->end()) {
        return found->second;
      }
    }
    for (auto it = intScopes.rbegin(); it != intScopes.rend(); ++it) {
      auto found = it->find(var->name);
      if (found != it->end()) {
        return static_cast<double>(found->second);
      }
    }
    throw std::runtime_error("unknown compile-time constant: " + var->name);
  }
  if (const auto *unary = dynamic_cast<const UnaryExpr *>(&expr)) {
    double value = evalConstFloat(*unary->operand, intScopes, floatScopes);
    if (unary->op == "+") return value;
    if (unary->op == "-") return -value;
    if (unary->op == "!") return value == 0.0 ? 1.0 : 0.0;
  }
  if (const auto *binary = dynamic_cast<const BinaryExpr *>(&expr)) {
    double lhs = evalConstFloat(*binary->lhs, intScopes, floatScopes);
    double rhs = evalConstFloat(*binary->rhs, intScopes, floatScopes);
    if (binary->op == "+") return lhs + rhs;
    if (binary->op == "-") return lhs - rhs;
    if (binary->op == "*") return lhs * rhs;
    if (binary->op == "/") return rhs == 0.0 ? 0.0 : lhs / rhs;
    if (binary->op == "==") return lhs == rhs;
    if (binary->op == "!=") return lhs != rhs;
    if (binary->op == "<") return lhs < rhs;
    if (binary->op == "<=") return lhs <= rhs;
    if (binary->op == ">") return lhs > rhs;
    if (binary->op == ">=") return lhs >= rhs;
    if (binary->op == "&&") return (lhs != 0.0) && (rhs != 0.0);
    if (binary->op == "||") return (lhs != 0.0) || (rhs != 0.0);
  }
  throw std::runtime_error("expression is not a compile-time float");
}

long long evalConstBits(const Expr &expr, BuiltinType type,
                        const std::vector<std::unordered_map<std::string, long long>> &constScopes,
                        const std::vector<std::unordered_map<std::string, double>> &floatScopes) {
  if (type == BuiltinType::Float) {
    return static_cast<long long>(floatBits(evalConstFloat(expr, constScopes, floatScopes)));
  }
  return evalConstInt(expr, constScopes);
}

long long elementCount(const std::vector<long long> &dims) {
  long long count = 1;
  for (long long dim : dims) {
    count *= dim;
  }
  return count;
}

void flattenConstInit(const InitVal *init, BuiltinType type,
                      std::vector<long long> &values,
                      const std::vector<std::unordered_map<std::string, long long>> &constScopes,
                      const std::vector<std::unordered_map<std::string, double>> &floatScopes) {
  if (!init) {
    return;
  }
  if (init->isList) {
    for (const auto &child : init->values) {
      flattenConstInit(child.get(), type, values, constScopes, floatScopes);
    }
    return;
  }
  if (init->expr) {
    values.push_back(evalConstBits(*init->expr, type, constScopes, floatScopes));
  }
}

void collectInitExprs(const InitVal *init, std::vector<const Expr *> &values) {
  if (!init) {
    return;
  }
  if (init->isList) {
    for (const auto &child : init->values) {
      collectInitExprs(child.get(), values);
    }
    return;
  }
  if (init->expr) {
    values.push_back(init->expr.get());
  }
}

void emitWordRun(std::ostream &os, long long value, long long count) {
  if (count <= 0) {
    return;
  }
  if (value == 0) {
    os << "\t.zero " << count * kWordSize << "\n";
    return;
  }
  if (count == 1) {
    os << "\t.word " << value << "\n";
    return;
  }
  os << "\t.rept " << count << "\n";
  os << "\t.word " << value << "\n";
  os << "\t.endr\n";
}

bool hasNonZeroValue(const std::vector<long long> &values) {
  return std::any_of(values.begin(), values.end(),
                     [](long long value) { return value != 0; });
}

bool isLiteralZero(const Expr &expr) {
  if (const auto *integer = dynamic_cast<const IntExpr *>(&expr)) {
    return integer->value == 0;
  }
  if (const auto *floating = dynamic_cast<const FloatExpr *>(&expr)) {
    return floating->value == 0.0;
  }
  return false;
}

class Generator {
public:
  Generator(const CompUnit &unit, std::ostream &os, RiscVCodeGenOptions options)
      : unit_(unit), os_(os), options_(options) {}

  void run() {
    (void)options_;
    collectFunctions();
    collectGlobals();
    emitData();
    emitText();
  }

private:
  struct FramePlan {
    long long nextOffset = 0;
    long long maxOutgoingArgs = 0;
  };

  const CompUnit &unit_;
  std::ostream &os_;
  RiscVCodeGenOptions options_;
  std::unordered_map<std::string, Symbol> globals_;
  std::unordered_map<std::string, FunctionInfo> functions_;
  std::vector<std::unordered_map<std::string, Symbol>> scopes_;
  std::vector<std::unordered_map<std::string, long long>> constScopes_;
  std::vector<std::unordered_map<std::string, double>> floatConstScopes_;
  std::vector<LoopLabels> loops_;
  std::unordered_map<const FuncParam *, Symbol> plannedParams_;
  std::unordered_map<const VarDef *, Symbol> plannedVars_;
  std::string currentFunction_;
  std::string currentReturnLabel_;
  BuiltinType currentReturnType_ = BuiltinType::Void;
  long long frameSize_ = 0;
  long long localAreaSize_ = 0;
  int labelId_ = 0;

  int valueSize(BuiltinType type) const {
    (void)type;
    return kWordSize;
  }

  void collectFunctions() {
    installBuiltin("getint", BuiltinType::Int, {});
    installBuiltin("getch", BuiltinType::Int, {});
    installBuiltin("getarray", BuiltinType::Int, {true});
    installBuiltin("getfloat", BuiltinType::Float, {});
    installBuiltin("getfarray", BuiltinType::Int, {true});
    installBuiltin("putint", BuiltinType::Void, {false});
    installBuiltin("putch", BuiltinType::Void, {false});
    installBuiltin("putarray", BuiltinType::Void, {false, true});
    installBuiltin("putfloat", BuiltinType::Void, {false});
    installBuiltin("putfarray", BuiltinType::Void, {false, true});
    installBuiltin("starttime", BuiltinType::Void, {});
    installBuiltin("stoptime", BuiltinType::Void, {});
    functions_["putfloat"].paramTypes = {BuiltinType::Float};
    functions_["putfarray"].paramTypes = {BuiltinType::Int, BuiltinType::Float};
    functions_["getfarray"].paramTypes = {BuiltinType::Float};

    for (const auto &decl : unit_.decls) {
      if (const auto *func = dynamic_cast<const FuncDef *>(decl.get())) {
        FunctionInfo info;
        info.returnType = func->returnType.base;
        for (const auto &param : func->params) {
          info.paramTypes.push_back(param->type.base);
          info.paramIsArray.push_back(param->isArray);
        }
        functions_[func->name] = std::move(info);
      }
    }
  }

  void installBuiltin(const std::string &name, BuiltinType ret, std::vector<bool> params) {
    FunctionInfo info;
    info.returnType = ret;
    for (bool isArray : params) {
      info.paramTypes.push_back(isArray ? BuiltinType::Int : BuiltinType::Int);
    }
    info.paramIsArray = std::move(params);
    functions_[name] = std::move(info);
  }

  void collectGlobals() {
    constScopes_.push_back({});
    floatConstScopes_.push_back({});
    for (const auto &decl : unit_.decls) {
      const auto *varDecl = dynamic_cast<const VarDecl *>(decl.get());
      if (!varDecl) {
        continue;
      }
      for (const auto &def : varDecl->defs) {
        Symbol sym;
        sym.storage = Symbol::Storage::Global;
        sym.type = varDecl->type.base;
        sym.isConst = varDecl->type.isConst;
        sym.isArray = !def->dimensions.empty();
        sym.label = sanitizeLabel(def->name);
        for (const auto &dim : def->dimensions) {
          sym.dims.push_back(evalConstInt(*dim, constScopes_));
        }
        globals_[def->name] = sym;
        if (sym.isConst && !sym.isArray && def->init && def->init->expr) {
          if (sym.type == BuiltinType::Float) {
            floatConstScopes_.back()[def->name] =
                evalConstFloat(*def->init->expr, constScopes_, floatConstScopes_);
          } else {
            constScopes_.back()[def->name] = evalConstInt(*def->init->expr, constScopes_);
          }
        }
      }
    }
  }

  void emitData() {
    for (const auto &decl : unit_.decls) {
      const auto *varDecl = dynamic_cast<const VarDecl *>(decl.get());
      if (!varDecl) {
        continue;
      }
      for (const auto &def : varDecl->defs) {
        const Symbol &sym = globals_.at(def->name);
        long long slots = sym.isArray ? elementCount(sym.dims) : 1;
        std::vector<long long> values;
        flattenConstInit(def->init.get(), sym.type, values, constScopes_,
                         floatConstScopes_);
        if (static_cast<long long>(values.size()) > slots) {
          values.resize(static_cast<std::size_t>(slots));
        }

        bool needsData = hasNonZeroValue(values);
        os_ << (needsData ? "\t.data\n" : "\t.bss\n");
        os_ << "\t.globl " << sym.label << "\n";
        os_ << "\t.align 2\n";
        os_ << sym.label << ":\n";
        if (!needsData) {
          emitWordRun(os_, 0, slots);
          continue;
        }

        long long emitted = 0;
        for (std::size_t i = 0; i < values.size();) {
          long long value = values[i];
          std::size_t j = i + 1;
          while (j < values.size() && values[j] == value) {
            ++j;
          }
          emitWordRun(os_, value, static_cast<long long>(j - i));
          emitted += static_cast<long long>(j - i);
          i = j;
        }
        if (emitted < slots) {
          emitWordRun(os_, 0, slots - emitted);
        }
      }
    }
  }

  void emitText() {
    os_ << "\t.text\n";
    for (const auto &decl : unit_.decls) {
      if (const auto *func = dynamic_cast<const FuncDef *>(decl.get())) {
        emitFunction(*func);
      }
    }
  }

  void emitFunction(const FuncDef &func) {
    currentFunction_ = sanitizeLabel(func.name);
    currentReturnType_ = func.returnType.base;
    currentReturnLabel_ = freshLabel(currentFunction_ + ".return");
    scopes_.clear();
    constScopes_.resize(1);
    floatConstScopes_.resize(1);
    loops_.clear();
    plannedParams_.clear();
    plannedVars_.clear();
    labelId_ = 0;

    FramePlan plan;
    pushScope();
    collectFrameForParams(func, plan);
    if (func.body) {
      collectFrameForBlock(*func.body, plan, false);
    }

    localAreaSize_ = alignTo(plan.nextOffset, 16);
    long long outgoingSize = alignTo(plan.maxOutgoingArgs * kPtrSize, 16);
    frameSize_ = alignTo(localAreaSize_ + outgoingSize + 16, 16);

    scopes_.clear();
    constScopes_.resize(1);
    floatConstScopes_.resize(1);
    pushScope();

    os_ << "\t.globl " << currentFunction_ << "\n";
    os_ << "\t.type " << currentFunction_ << ", @function\n";
    os_ << currentFunction_ << ":\n";
    emitAddi("sp", "sp", -frameSize_);
    emitSd("ra", frameSize_ - 8, "sp");
    emitSd("s0", frameSize_ - 16, "sp");
    emitAddi("s0", "sp", frameSize_);

    bindParams(func);
    if (func.body) {
      genBlock(*func.body, false);
    }
    if (func.returnType.base == BuiltinType::Void) {
      os_ << "\tj " << currentReturnLabel_ << "\n";
    } else {
      os_ << "\tli a0, 0\n";
      os_ << "\tj " << currentReturnLabel_ << "\n";
    }

    os_ << currentReturnLabel_ << ":\n";
    emitLd("ra", frameSize_ - 8, "sp");
    emitLd("s0", frameSize_ - 16, "sp");
    emitAddi("sp", "sp", frameSize_);
    os_ << "\tret\n";
    os_ << "\t.size " << currentFunction_ << ", .-" << currentFunction_ << "\n";

    popScope();
    currentFunction_.clear();
    currentReturnType_ = BuiltinType::Void;
  }

  void collectFrameForParams(const FuncDef &func, FramePlan &plan) {
    for (const auto &param : func.params) {
      Symbol sym;
      sym.type = param->type.base;
      sym.isArray = param->isArray;
      sym.storage = param->isArray ? Symbol::Storage::ParamPtr : Symbol::Storage::Local;
      for (const auto &dim : param->dimensions) {
        sym.dims.push_back(evalConstInt(*dim, constScopes_));
      }
      int size = param->isArray ? kPtrSize : valueSize(param->type.base);
      sym.offset = allocateSlot(plan, size, size);
      scopes_.back()[param->name] = sym;
      plannedParams_[param.get()] = sym;
    }
  }

  void collectFrameForBlock(const BlockStmt &block, FramePlan &plan, bool createsScope = true) {
    if (createsScope) {
      pushScope();
    }
    for (const auto &item : block.items) {
      if (item.decl) {
        if (const auto *decl = dynamic_cast<const VarDecl *>(item.decl.get())) {
          collectFrameForDecl(*decl, plan);
        }
      } else if (item.stmt) {
        collectFrameForStmt(*item.stmt, plan);
      }
    }
    if (createsScope) {
      popScope();
    }
  }

  void collectFrameForDecl(const VarDecl &decl, FramePlan &plan) {
    for (const auto &def : decl.defs) {
      Symbol sym;
      sym.type = decl.type.base;
      sym.isConst = decl.type.isConst;
      sym.isArray = !def->dimensions.empty();
      for (const auto &dim : def->dimensions) {
        sym.dims.push_back(evalConstInt(*dim, constScopes_));
      }
      long long bytes = sym.isArray ? elementCount(sym.dims) * valueSize(sym.type)
                                    : valueSize(sym.type);
      sym.offset = allocateSlot(plan, bytes, valueSize(sym.type));
      scopes_.back()[def->name] = sym;
      plannedVars_[def.get()] = sym;
      if (sym.isConst && !sym.isArray && def->init && def->init->expr) {
        if (sym.type == BuiltinType::Float) {
          floatConstScopes_.back()[def->name] =
              evalConstFloat(*def->init->expr, constScopes_, floatConstScopes_);
        } else {
          constScopes_.back()[def->name] = evalConstInt(*def->init->expr, constScopes_);
        }
      }
    }
  }

  void collectFrameForStmt(const Stmt &stmt, FramePlan &plan) {
    if (const auto *block = dynamic_cast<const BlockStmt *>(&stmt)) {
      collectFrameForBlock(*block, plan);
      return;
    }
    if (const auto *ifs = dynamic_cast<const IfStmt *>(&stmt)) {
      collectFrameForStmt(*ifs->thenBranch, plan);
      if (ifs->elseBranch) {
        collectFrameForStmt(*ifs->elseBranch, plan);
      }
      return;
    }
    if (const auto *whileStmt = dynamic_cast<const WhileStmt *>(&stmt)) {
      collectFrameForStmt(*whileStmt->body, plan);
      return;
    }
    if (const auto *exprStmt = dynamic_cast<const ExprStmt *>(&stmt)) {
      if (exprStmt->expr) collectFrameForExpr(*exprStmt->expr, plan);
      return;
    }
    if (const auto *assign = dynamic_cast<const AssignStmt *>(&stmt)) {
      collectFrameForExpr(*assign->value, plan);
      for (const auto &index : assign->target->indices) collectFrameForExpr(*index, plan);
      return;
    }
    if (const auto *ret = dynamic_cast<const ReturnStmt *>(&stmt)) {
      if (ret->value) collectFrameForExpr(*ret->value, plan);
    }
  }

  void collectFrameForExpr(const Expr &expr, FramePlan &plan) {
    if (const auto *call = dynamic_cast<const CallExpr *>(&expr)) {
      if (call->args.size() > 8) {
        plan.maxOutgoingArgs =
            std::max<long long>(plan.maxOutgoingArgs, static_cast<long long>(call->args.size() - 8));
      }
      for (const auto &arg : call->args) collectFrameForExpr(*arg, plan);
      return;
    }
    if (const auto *unary = dynamic_cast<const UnaryExpr *>(&expr)) {
      collectFrameForExpr(*unary->operand, plan);
      return;
    }
    if (const auto *binary = dynamic_cast<const BinaryExpr *>(&expr)) {
      collectFrameForExpr(*binary->lhs, plan);
      collectFrameForExpr(*binary->rhs, plan);
      return;
    }
    if (const auto *var = dynamic_cast<const VarExpr *>(&expr)) {
      for (const auto &index : var->indices) collectFrameForExpr(*index, plan);
    }
  }

  long long allocateSlot(FramePlan &plan, long long bytes, long long align) {
    plan.nextOffset = alignTo(plan.nextOffset, align);
    plan.nextOffset += alignTo(bytes, align);
    return -plan.nextOffset;
  }

  void bindParams(const FuncDef &func) {
    int intReg = 0;
    int floatReg = 0;
    long long stackArgOffset = 0;
    for (std::size_t i = 0; i < func.params.size(); ++i) {
      const auto &param = func.params[i];
      auto planned = plannedParams_.find(param.get());
      assert(planned != plannedParams_.end());
      Symbol sym = planned->second;
      bind(param->name, sym);

      if (!param->isArray && param->type.base == BuiltinType::Float && floatReg < 8) {
        emitFsw("fa" + std::to_string(floatReg++), sym.offset, "s0");
        continue;
      }

      if ((param->isArray || param->type.base != BuiltinType::Float) && intReg < 8) {
        if (param->isArray) {
          emitSd("a" + std::to_string(intReg), sym.offset, "s0");
        } else {
          emitSw("a" + std::to_string(intReg), sym.offset, "s0");
        }
        ++intReg;
        continue;
      }

      long long callerOffset = stackArgOffset;
      stackArgOffset += kPtrSize;
      if (param->type.base == BuiltinType::Float && !param->isArray) {
        emitLd("t0", callerOffset, "s0");
        os_ << "\tfmv.w.x ft0, t0\n";
        emitFsw("ft0", sym.offset, "s0");
      } else {
        emitLd("t0", callerOffset, "s0");
        if (param->isArray) emitSd("t0", sym.offset, "s0");
        else emitSw("t0", sym.offset, "s0");
      }
    }
  }

  void genBlock(const BlockStmt &block, bool createsScope = true) {
    if (createsScope) {
      pushScope();
    }
    for (const auto &item : block.items) {
      if (item.decl) {
        if (const auto *decl = dynamic_cast<const VarDecl *>(item.decl.get())) {
          genDecl(*decl);
        }
      } else if (item.stmt) {
        genStmt(*item.stmt);
      }
    }
    if (createsScope) {
      popScope();
    }
  }

  void genDecl(const VarDecl &decl) {
    for (const auto &def : decl.defs) {
      auto planned = plannedVars_.find(def.get());
      assert(planned != plannedVars_.end());
      Symbol sym = planned->second;
      bind(def->name, sym);
      if (sym.isConst && !sym.isArray && def->init && def->init->expr) {
        constScopes_.back()[def->name] = evalConstInt(*def->init->expr, constScopes_);
      }
      if (sym.isArray) {
        if (!def->init) {
          continue;
        }

        std::vector<const Expr *> values;
        collectInitExprs(def->init.get(), values);
        long long slots = elementCount(sym.dims);
        if (static_cast<long long>(values.size()) > slots) {
          values.resize(static_cast<std::size_t>(slots));
        }

        std::size_t literalZeros = std::count_if(
            values.begin(), values.end(),
            [](const Expr *expr) { return expr && isLiteralZero(*expr); });
        bool partialInit = static_cast<long long>(values.size()) < slots;
        bool clearFirst =
            partialInit || (literalZeros > 8 && literalZeros * 2 >= values.size());
        if (clearFirst) {
          zeroArray(sym);
        }

        for (std::size_t i = 0; i < values.size(); ++i) {
          if (clearFirst && values[i] && isLiteralZero(*values[i])) {
            continue;
          }
          genExpr(*values[i]);
          if (sym.type == BuiltinType::Float) {
            emitFsw("fa0", sym.offset + static_cast<long long>(i) * kWordSize, "s0");
          } else {
            emitSw("a0", sym.offset + static_cast<long long>(i) * kWordSize, "s0");
          }
        }
      } else if (def->init && def->init->expr) {
        genExpr(*def->init->expr);
        if (sym.type == BuiltinType::Float) {
          emitFsw("fa0", sym.offset, "s0");
        } else {
          emitSw("a0", sym.offset, "s0");
        }
      } else {
        os_ << "\tli t0, 0\n";
        emitSw("t0", sym.offset, "s0");
      }
    }
  }

  void zeroArray(const Symbol &sym) {
    long long slots = elementCount(sym.dims);
    if (slots <= 0) {
      return;
    }
    if (slots <= 8) {
      os_ << "\tli t0, 0\n";
      for (long long i = 0; i < slots; ++i) {
        emitSw("t0", sym.offset + i * kWordSize, "s0");
      }
      return;
    }
    std::string loopLabel = freshLabel("zero_loop");
    std::string endLabel = freshLabel("zero_end");
    emitAddi("t1", "s0", sym.offset);
    os_ << "\tli t2, " << slots << "\n";
    os_ << "\tli t0, 0\n";
    os_ << loopLabel << ":\n";
    os_ << "\tbeqz t2, " << endLabel << "\n";
    emitSw("t0", 0, "t1");
    os_ << "\taddi t1, t1, 4\n";
    os_ << "\taddi t2, t2, -1\n";
    os_ << "\tj " << loopLabel << "\n";
    os_ << endLabel << ":\n";
  }

  void genStmt(const Stmt &stmt) {
    if (const auto *block = dynamic_cast<const BlockStmt *>(&stmt)) {
      genBlock(*block);
      return;
    }
    if (dynamic_cast<const EmptyStmt *>(&stmt)) {
      return;
    }
    if (const auto *exprStmt = dynamic_cast<const ExprStmt *>(&stmt)) {
      if (exprStmt->expr) genExpr(*exprStmt->expr);
      return;
    }
    if (const auto *assign = dynamic_cast<const AssignStmt *>(&stmt)) {
      const Symbol *targetSym = lookupAny(assign->target->name);
      genAddress(*assign->target);
      pushA0();
      genExpr(*assign->value);
      BuiltinType valueType = exprType(*assign->value);
      popTo("t0");
      BuiltinType targetType = targetSym ? targetSym->type : BuiltinType::Int;
      convertValue(valueType, targetType);
      if (targetType == BuiltinType::Float) {
        emitFsw("fa0", 0, "t0");
      } else {
        emitSw("a0", 0, "t0");
      }
      return;
    }
    if (const auto *ifs = dynamic_cast<const IfStmt *>(&stmt)) {
      std::string thenLabel = freshLabel("if_then");
      std::string elseLabel = ifs->elseBranch ? freshLabel("if_else") : "";
      std::string endLabel = freshLabel("if_end");
      genCond(*ifs->cond, thenLabel, ifs->elseBranch ? elseLabel : endLabel);
      os_ << thenLabel << ":\n";
      genStmt(*ifs->thenBranch);
      os_ << "\tj " << endLabel << "\n";
      if (ifs->elseBranch) {
        os_ << elseLabel << ":\n";
        genStmt(*ifs->elseBranch);
        os_ << "\tj " << endLabel << "\n";
      }
      os_ << endLabel << ":\n";
      return;
    }
    if (const auto *whileStmt = dynamic_cast<const WhileStmt *>(&stmt)) {
      std::string condLabel = freshLabel("while_cond");
      std::string bodyLabel = freshLabel("while_body");
      std::string endLabel = freshLabel("while_end");
      os_ << condLabel << ":\n";
      genCond(*whileStmt->cond, bodyLabel, endLabel);
      os_ << bodyLabel << ":\n";
      loops_.push_back(LoopLabels{condLabel, endLabel});
      genStmt(*whileStmt->body);
      loops_.pop_back();
      os_ << "\tj " << condLabel << "\n";
      os_ << endLabel << ":\n";
      return;
    }
    if (dynamic_cast<const BreakStmt *>(&stmt)) {
      os_ << "\tj " << loops_.back().breakLabel << "\n";
      return;
    }
    if (dynamic_cast<const ContinueStmt *>(&stmt)) {
      os_ << "\tj " << loops_.back().continueLabel << "\n";
      return;
    }
    if (const auto *ret = dynamic_cast<const ReturnStmt *>(&stmt)) {
      if (ret->value) {
        genExpr(*ret->value);
        BuiltinType valueType = exprType(*ret->value);
        BuiltinType returnType = currentReturnType_;
        if (returnType == BuiltinType::Float && valueType != BuiltinType::Float) {
          os_ << "\tfcvt.s.w fa0, a0\n";
        } else if (returnType == BuiltinType::Int && valueType == BuiltinType::Float) {
          os_ << "\tfcvt.w.s a0, fa0, rtz\n";
        }
      }
      os_ << "\tj " << currentReturnLabel_ << "\n";
    }
  }

  void genExpr(const Expr &expr) {
    if (const auto *integer = dynamic_cast<const IntExpr *>(&expr)) {
      os_ << "\tli a0, " << integer->value << "\n";
      return;
    }
    if (const auto *floating = dynamic_cast<const FloatExpr *>(&expr)) {
      os_ << "\tli t0, " << floatBits(floating->value) << "\n";
      os_ << "\tfmv.w.x fa0, t0\n";
      return;
    }
    if (const auto *var = dynamic_cast<const VarExpr *>(&expr)) {
      const Symbol *sym = lookupAny(var->name);
      if (sym && sym->isArray && var->indices.empty()) {
        genAddress(*var);
        return;
      }
      genAddress(*var);
      if (sym && sym->type == BuiltinType::Float) {
        emitFlw("fa0", 0, "a0");
      } else {
        emitLw("a0", 0, "a0");
      }
      return;
    }
    if (const auto *unary = dynamic_cast<const UnaryExpr *>(&expr)) {
      genExpr(*unary->operand);
      if (unary->op == "+") {
        return;
      }
      if (unary->op == "-") {
        if (exprType(*unary->operand) == BuiltinType::Float) {
          os_ << "\tfneg.s fa0, fa0\n";
        } else {
          os_ << "\tneg a0, a0\n";
        }
        return;
      }
      if (unary->op == "!") {
        if (exprType(*unary->operand) == BuiltinType::Float) {
          os_ << "\tli t0, 0\n";
          os_ << "\tfmv.w.x ft0, t0\n";
          os_ << "\tfeq.s a0, fa0, ft0\n";
        } else {
          os_ << "\tseqz a0, a0\n";
        }
        return;
      }
    }
    if (const auto *binary = dynamic_cast<const BinaryExpr *>(&expr)) {
      if (binary->op == "&&" || binary->op == "||") {
        genLogicalValue(*binary);
      } else {
        genBinary(*binary);
      }
      return;
    }
    if (const auto *call = dynamic_cast<const CallExpr *>(&expr)) {
      genCall(*call);
      return;
    }
    throw std::runtime_error("unsupported expression");
  }

  void genBinary(const BinaryExpr &expr) {
    BuiltinType lhsType = exprType(*expr.lhs);
    BuiltinType rhsType = exprType(*expr.rhs);
    if (lhsType == BuiltinType::Float || rhsType == BuiltinType::Float) {
      genFloatBinary(expr, lhsType, rhsType);
      return;
    }

    genExpr(*expr.lhs);
    pushA0();
    genExpr(*expr.rhs);
    popTo("t0");

    if (expr.op == "+") os_ << "\tadd a0, t0, a0\n";
    else if (expr.op == "-") os_ << "\tsub a0, t0, a0\n";
    else if (expr.op == "*") os_ << "\tmul a0, t0, a0\n";
    else if (expr.op == "/") os_ << "\tdiv a0, t0, a0\n";
    else if (expr.op == "%") os_ << "\trem a0, t0, a0\n";
    else if (expr.op == "==") {
      os_ << "\tsub a0, t0, a0\n";
      os_ << "\tseqz a0, a0\n";
    } else if (expr.op == "!=") {
      os_ << "\tsub a0, t0, a0\n";
      os_ << "\tsnez a0, a0\n";
    } else if (expr.op == "<") {
      os_ << "\tslt a0, t0, a0\n";
    } else if (expr.op == "<=") {
      os_ << "\tslt a0, a0, t0\n";
      os_ << "\txori a0, a0, 1\n";
    } else if (expr.op == ">") {
      os_ << "\tslt a0, a0, t0\n";
    } else if (expr.op == ">=") {
      os_ << "\tslt a0, t0, a0\n";
      os_ << "\txori a0, a0, 1\n";
    } else {
      throw std::runtime_error("unsupported binary operator: " + expr.op);
    }
  }

  void genFloatBinary(const BinaryExpr &expr, BuiltinType lhsType, BuiltinType rhsType) {
    genExpr(*expr.lhs);
    if (lhsType != BuiltinType::Float) {
      os_ << "\tfcvt.s.w fa0, a0\n";
    }
    pushFa0();
    genExpr(*expr.rhs);
    if (rhsType != BuiltinType::Float) {
      os_ << "\tfcvt.s.w fa0, a0\n";
    }
    popFTo("ft0");

    if (expr.op == "+") os_ << "\tfadd.s fa0, ft0, fa0\n";
    else if (expr.op == "-") os_ << "\tfsub.s fa0, ft0, fa0\n";
    else if (expr.op == "*") os_ << "\tfmul.s fa0, ft0, fa0\n";
    else if (expr.op == "/") os_ << "\tfdiv.s fa0, ft0, fa0\n";
    else if (expr.op == "==") os_ << "\tfeq.s a0, ft0, fa0\n";
    else if (expr.op == "!=") {
      os_ << "\tfeq.s a0, ft0, fa0\n";
      os_ << "\txori a0, a0, 1\n";
    } else if (expr.op == "<") os_ << "\tflt.s a0, ft0, fa0\n";
    else if (expr.op == "<=") os_ << "\tfle.s a0, ft0, fa0\n";
    else if (expr.op == ">") os_ << "\tflt.s a0, fa0, ft0\n";
    else if (expr.op == ">=") os_ << "\tfle.s a0, fa0, ft0\n";
    else throw std::runtime_error("unsupported float binary operator: " + expr.op);
  }

  void genLogicalValue(const BinaryExpr &expr) {
    std::string trueLabel = freshLabel("logic_true");
    std::string falseLabel = freshLabel("logic_false");
    std::string endLabel = freshLabel("logic_end");
    genCond(expr, trueLabel, falseLabel);
    os_ << trueLabel << ":\n";
    os_ << "\tli a0, 1\n";
    os_ << "\tj " << endLabel << "\n";
    os_ << falseLabel << ":\n";
    os_ << "\tli a0, 0\n";
    os_ << endLabel << ":\n";
  }

  void genCond(const Expr &expr, const std::string &trueLabel,
               const std::string &falseLabel) {
    if (const auto *binary = dynamic_cast<const BinaryExpr *>(&expr)) {
      if (binary->op == "&&") {
        std::string rhsLabel = freshLabel("land_rhs");
        genCond(*binary->lhs, rhsLabel, falseLabel);
        os_ << rhsLabel << ":\n";
        genCond(*binary->rhs, trueLabel, falseLabel);
        return;
      }
      if (binary->op == "||") {
        std::string rhsLabel = freshLabel("lor_rhs");
        genCond(*binary->lhs, trueLabel, rhsLabel);
        os_ << rhsLabel << ":\n";
        genCond(*binary->rhs, trueLabel, falseLabel);
        return;
      }
    }
    genExpr(expr);
    if (exprType(expr) == BuiltinType::Float) {
      os_ << "\tli t0, 0\n";
      os_ << "\tfmv.w.x ft0, t0\n";
      os_ << "\tfeq.s a0, fa0, ft0\n";
      os_ << "\tbeqz a0, " << trueLabel << "\n";
    } else {
      os_ << "\tbnez a0, " << trueLabel << "\n";
    }
    os_ << "\tj " << falseLabel << "\n";
  }

  void genCall(const CallExpr &call) {
    for (std::size_t i = call.args.size(); i > 0; --i) {
      const Expr &arg = *call.args[i - 1];
      BuiltinType fromType = exprType(arg);
      BuiltinType toType = expectedArgType(call, i - 1);
      bool argArray = isArrayPointerArg(arg);
      genExpr(arg);
      if (!argArray) {
        convertValue(fromType, toType);
      }
      if (toType == BuiltinType::Float && !argArray) {
        pushFa0();
      } else {
        pushA0();
      }
    }
    int intReg = 0;
    int floatReg = 0;
    long long stackOffset = 0;
    for (std::size_t i = 0; i < call.args.size(); ++i) {
      BuiltinType argType = expectedArgType(call, i);
      bool argArray = isArrayPointerArg(*call.args[i]);
      if (argType == BuiltinType::Float && !argArray && floatReg < 8) {
        popFTo("fa" + std::to_string(floatReg++));
      } else if ((argType != BuiltinType::Float || argArray) && intReg < 8) {
        popTo("t0");
        os_ << "\tmv a" << intReg++ << ", t0\n";
      } else {
        if (argType == BuiltinType::Float) {
          popFTo("ft0");
          long long finalSpOffset =
              static_cast<long long>(call.args.size() - i - 1) * 16;
          emitFsw("ft0", finalSpOffset + stackOffset, "sp");
        } else {
          popTo("t0");
          long long finalSpOffset =
              static_cast<long long>(call.args.size() - i - 1) * 16;
          emitSd("t0", finalSpOffset + stackOffset, "sp");
        }
        stackOffset += kPtrSize;
      }
    }
    os_ << "\tcall " << sanitizeLabel(call.callee) << "\n";
  }

  BuiltinType expectedArgType(const CallExpr &call, std::size_t index) const {
    auto found = functions_.find(call.callee);
    if (found == functions_.end() || index >= found->second.paramTypes.size()) {
      return exprType(*call.args[index]);
    }
    return found->second.paramTypes[index];
  }

  bool isArrayPointerArg(const Expr &expr) const {
    const auto *var = dynamic_cast<const VarExpr *>(&expr);
    if (!var || !var->indices.empty()) {
      return false;
    }
    const Symbol *sym = lookupAny(var->name);
    return sym && sym->isArray;
  }

  void genAddress(const VarExpr &expr) {
    const Symbol *sym = lookupAny(expr.name);
    if (!sym) {
      throw std::runtime_error("unknown variable: " + expr.name);
    }

    if (sym->storage == Symbol::Storage::Global) {
      os_ << "\tla a0, " << sym->label << "\n";
    } else if (sym->storage == Symbol::Storage::ParamPtr) {
      emitLd("a0", sym->offset, "s0");
    } else {
      emitAddi("a0", "s0", sym->offset);
    }

    if (expr.indices.empty()) {
      return;
    }

    pushA0();
    for (std::size_t i = 0; i < expr.indices.size(); ++i) {
      genExpr(*expr.indices[i]);
      long long stride = indexStride(*sym, i);
      if (stride != 1) {
        os_ << "\tli t0, " << stride << "\n";
        os_ << "\tmul a0, a0, t0\n";
      }
      os_ << "\tslli a0, a0, 2\n";
      popTo("t0");
      os_ << "\tadd a0, t0, a0\n";
      if (i + 1 < expr.indices.size()) {
        pushA0();
      }
    }
  }

  long long indexStride(const Symbol &sym, std::size_t index) const {
    long long stride = 1;
    std::size_t firstDim =
        sym.storage == Symbol::Storage::ParamPtr ? index : index + 1;
    for (std::size_t j = firstDim; j < sym.dims.size(); ++j) {
      stride *= sym.dims[j];
    }
    return stride;
  }

  void pushA0() {
    emitAddi("sp", "sp", -16);
    emitSd("a0", 8, "sp");
  }

  void pushFa0() {
    emitAddi("sp", "sp", -16);
    emitFsw("fa0", 8, "sp");
  }

  void popTo(const std::string &reg) {
    emitLd(reg, 8, "sp");
    emitAddi("sp", "sp", 16);
  }

  void popFTo(const std::string &reg) {
    emitFlw(reg, 8, "sp");
    emitAddi("sp", "sp", 16);
  }

  BuiltinType exprType(const Expr &expr) const {
    if (dynamic_cast<const FloatExpr *>(&expr)) {
      return BuiltinType::Float;
    }
    if (dynamic_cast<const IntExpr *>(&expr)) {
      return BuiltinType::Int;
    }
    if (const auto *var = dynamic_cast<const VarExpr *>(&expr)) {
      const Symbol *sym = lookupAny(var->name);
      return sym ? sym->type : BuiltinType::Int;
    }
    if (const auto *unary = dynamic_cast<const UnaryExpr *>(&expr)) {
      if (unary->op == "!") {
        return BuiltinType::Int;
      }
      return exprType(*unary->operand);
    }
    if (const auto *binary = dynamic_cast<const BinaryExpr *>(&expr)) {
      if (binary->op == "&&" || binary->op == "||" || binary->op == "==" ||
          binary->op == "!=" || binary->op == "<" || binary->op == "<=" ||
          binary->op == ">" || binary->op == ">=") {
        return BuiltinType::Int;
      }
      BuiltinType lhs = exprType(*binary->lhs);
      BuiltinType rhs = exprType(*binary->rhs);
      return lhs == BuiltinType::Float || rhs == BuiltinType::Float
                 ? BuiltinType::Float
                 : BuiltinType::Int;
    }
    if (const auto *call = dynamic_cast<const CallExpr *>(&expr)) {
      auto found = functions_.find(call->callee);
      return found == functions_.end() ? BuiltinType::Int : found->second.returnType;
    }
    return BuiltinType::Int;
  }

  void convertValue(BuiltinType from, BuiltinType to) {
    if (from == to || from == BuiltinType::Unknown || to == BuiltinType::Unknown ||
        to == BuiltinType::Void) {
      return;
    }
    if (from == BuiltinType::Int && to == BuiltinType::Float) {
      os_ << "\tfcvt.s.w fa0, a0\n";
    } else if (from == BuiltinType::Float && to == BuiltinType::Int) {
      os_ << "\tfcvt.w.s a0, fa0, rtz\n";
    }
  }

  void pushScope() {
    scopes_.emplace_back();
    constScopes_.emplace_back();
    floatConstScopes_.emplace_back();
  }

  void popScope() {
    scopes_.pop_back();
    constScopes_.pop_back();
    floatConstScopes_.pop_back();
  }

  void bind(const std::string &name, Symbol sym) { scopes_.back()[name] = std::move(sym); }

  const Symbol *lookup(const std::string &name) const {
    if (scopes_.empty()) {
      return nullptr;
    }
    auto found = scopes_.back().find(name);
    if (found == scopes_.back().end()) {
      return nullptr;
    }
    return &found->second;
  }

  const Symbol *lookupAny(const std::string &name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
      auto found = it->find(name);
      if (found != it->end()) {
        return &found->second;
      }
    }
    auto global = globals_.find(name);
    if (global != globals_.end()) {
      return &global->second;
    }
    return nullptr;
  }

  std::string freshLabel(const std::string &prefix) {
    return ".L" + sanitizeLabel(prefix) + "_" + std::to_string(labelId_++);
  }

  void emitAddi(const std::string &rd, const std::string &rs, long long imm) {
    if (imm >= -2048 && imm <= 2047) {
      os_ << "\taddi " << rd << ", " << rs << ", " << imm << "\n";
    } else {
      os_ << "\tli t6, " << imm << "\n";
      os_ << "\tadd " << rd << ", " << rs << ", t6\n";
    }
  }

  void emitLw(const std::string &rd, long long offset, const std::string &base) {
    emitMem("lw", rd, offset, base);
  }

  void emitSw(const std::string &rs, long long offset, const std::string &base) {
    emitMem("sw", rs, offset, base);
  }

  void emitFlw(const std::string &rd, long long offset, const std::string &base) {
    emitMem("flw", rd, offset, base);
  }

  void emitFsw(const std::string &rs, long long offset, const std::string &base) {
    emitMem("fsw", rs, offset, base);
  }

  void emitLd(const std::string &rd, long long offset, const std::string &base) {
    emitMem("ld", rd, offset, base);
  }

  void emitSd(const std::string &rs, long long offset, const std::string &base) {
    emitMem("sd", rs, offset, base);
  }

  void emitMem(const std::string &op, const std::string &reg, long long offset,
               const std::string &base) {
    if (offset >= -2048 && offset <= 2047) {
      os_ << "\t" << op << " " << reg << ", " << offset << "(" << base << ")\n";
    } else {
      os_ << "\tli t6, " << offset << "\n";
      os_ << "\tadd t6, " << base << ", t6\n";
      os_ << "\t" << op << " " << reg << ", 0(t6)\n";
    }
  }
};

} // namespace

RiscVCodeGenerator::RiscVCodeGenerator(RiscVCodeGenOptions options)
    : options_(options) {}

void RiscVCodeGenerator::generate(const CompUnit &unit, std::ostream &os) {
  Generator(unit, os, options_).run();
}

} // namespace by::backend
