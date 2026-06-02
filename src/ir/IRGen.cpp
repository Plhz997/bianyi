#include "IRGen.h"

#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace by::ir {

namespace {

std::string baseType(BuiltinType type) {
  switch (type) {
  case BuiltinType::Int:
    return "i32";
  case BuiltinType::Float:
    return "f32";
  case BuiltinType::Void:
    return "void";
  case BuiltinType::Unknown:
    return "unknown";
  }
  return "unknown";
}

bool isVoidType(const std::string &type) { return type == "void"; }

} // namespace

Module IRGenerator::generate(const CompUnit &unit) {
  module_ = Module{};
  currentFunction_ = nullptr;
  currentBlock_ = nullptr;
  scopes_.clear();
  globals_.clear();
  functionReturns_.clear();
  loops_.clear();
  tempId_ = 0;
  labelId_ = 0;
  localId_ = 0;

  collectFunctionTypes(unit);
  for (const auto &decl : unit.decls) {
    genTopLevelDecl(*decl);
  }
  return module_;
}

void IRGenerator::collectFunctionTypes(const CompUnit &unit) {
  functionReturns_ = {
      {"getint", "i32"},     {"getch", "i32"},
      {"getarray", "i32"},   {"getfloat", "f32"},
      {"getfarray", "i32"},  {"putint", "void"},
      {"putch", "void"},     {"putarray", "void"},
      {"putfloat", "void"},  {"putfarray", "void"},
      {"starttime", "void"}, {"stoptime", "void"},
  };

  for (const auto &decl : unit.decls) {
    if (const auto *func = dynamic_cast<const FuncDef *>(decl.get())) {
      functionReturns_[func->name] = baseType(func->returnType.base);
    }
  }
}

void IRGenerator::genTopLevelDecl(const Decl &decl) {
  if (const auto *var = dynamic_cast<const VarDecl *>(&decl)) {
    genGlobalDecl(*var);
    return;
  }

  if (const auto *func = dynamic_cast<const FuncDef *>(&decl)) {
    genFunction(*func);
  }
}

void IRGenerator::genGlobalDecl(const VarDecl &decl) {
  genVarDecl(decl, true);
}

void IRGenerator::genFunction(const FuncDef &func) {
  module_.functions.push_back(Function{});
  currentFunction_ = &module_.functions.back();
  currentFunction_->name = func.name;
  currentFunction_->returnType = baseType(func.returnType.base);

  scopes_.clear();
  pushScope();

  for (const auto &param : func.params) {
    std::string type = renderType(param->type, param->isArray,
                                  param->isArray ? &param->dimensions : nullptr);
    std::string paramValue = "%" + valueName(param->name);
    currentFunction_->params.push_back(paramValue + " : " + type);
  }

  startBlock("entry");

  for (const auto &param : func.params) {
    std::string type = renderType(param->type, param->isArray,
                                  param->isArray ? &param->dimensions : nullptr);
    std::string paramValue = "%" + valueName(param->name);
    if (param->isArray) {
      bind(param->name, Binding{paramValue, baseType(param->type.base), true});
      continue;
    }

    std::string slot = "%" + freshLocalName(param->name + ".addr");
    emit(slot + " = alloca " + type);
    emit("store " + paramValue + ", " + slot);
    bind(param->name, Binding{slot, type, false});
  }

  if (func.body) {
    genBlock(*func.body, false);
  }

  if (!currentBlockTerminated()) {
    if (currentFunction_->returnType == "void") {
      terminate("ret");
    } else {
      terminate("ret 0");
    }
  }

  popScope();
  currentFunction_ = nullptr;
  currentBlock_ = nullptr;
}

void IRGenerator::genBlock(const BlockStmt &block, bool createsScope) {
  if (createsScope) {
    pushScope();
  }

  for (const auto &item : block.items) {
    genBlockItem(item);
  }

  if (createsScope) {
    popScope();
  }
}

void IRGenerator::genBlockItem(const BlockItem &item) {
  if (item.decl) {
    if (const auto *decl = dynamic_cast<const VarDecl *>(item.decl.get())) {
      genVarDecl(*decl, false);
    }
  } else if (item.stmt) {
    genStmt(*item.stmt);
  }
}

void IRGenerator::genVarDecl(const VarDecl &decl, bool global) {
  for (const auto &def : decl.defs) {
    std::string type =
        renderType(decl.type, !def->dimensions.empty(), &def->dimensions);

    if (global) {
      std::string init = renderInit(def->init.get());
      module_.globals.push_back(Global{def->name, type, init});
      globals_[def->name] = Binding{"@" + def->name, baseType(decl.type.base),
                                    !def->dimensions.empty()};
      continue;
    }

    std::string slot = "%" + freshLocalName(def->name + ".addr");
    emit(slot + " = alloca " + type);
    bind(def->name,
         Binding{slot, baseType(decl.type.base), !def->dimensions.empty()});

    if (def->init) {
      if (def->dimensions.empty() && def->init->expr) {
        emit("store " + genExpr(*def->init->expr) + ", " + slot);
      } else {
        emit("init " + slot + ", " + renderInit(def->init.get()));
      }
    }
  }
}

void IRGenerator::genStmt(const Stmt &stmt) {
  if (currentBlockTerminated()) {
    return;
  }

  if (const auto *block = dynamic_cast<const BlockStmt *>(&stmt)) {
    genBlock(*block);
    return;
  }

  if (const auto *empty = dynamic_cast<const EmptyStmt *>(&stmt)) {
    (void)empty;
    return;
  }

  if (const auto *exprStmt = dynamic_cast<const ExprStmt *>(&stmt)) {
    if (exprStmt->expr) {
      genExpr(*exprStmt->expr);
    }
    return;
  }

  if (const auto *assign = dynamic_cast<const AssignStmt *>(&stmt)) {
    std::string target = genLValue(*assign->target);
    std::string value = genExpr(*assign->value);
    emit("store " + value + ", " + target);
    return;
  }

  if (const auto *ifs = dynamic_cast<const IfStmt *>(&stmt)) {
    std::string thenLabel = freshLabel("if.then");
    std::string elseLabel = ifs->elseBranch ? freshLabel("if.else") : "";
    std::string endLabel = freshLabel("if.end");
    genCond(*ifs->cond, thenLabel, ifs->elseBranch ? elseLabel : endLabel);

    startBlock(thenLabel);
    genStmt(*ifs->thenBranch);
    if (!currentBlockTerminated()) {
      terminate("br " + endLabel);
    }

    if (ifs->elseBranch) {
      startBlock(elseLabel);
      genStmt(*ifs->elseBranch);
      if (!currentBlockTerminated()) {
        terminate("br " + endLabel);
      }
    }

    startBlock(endLabel);
    return;
  }

  if (const auto *whileStmt = dynamic_cast<const WhileStmt *>(&stmt)) {
    std::string condLabel = freshLabel("while.cond");
    std::string bodyLabel = freshLabel("while.body");
    std::string endLabel = freshLabel("while.end");

    terminate("br " + condLabel);
    startBlock(condLabel);
    genCond(*whileStmt->cond, bodyLabel, endLabel);

    startBlock(bodyLabel);
    loops_.push_back(LoopTarget{condLabel, endLabel});
    genStmt(*whileStmt->body);
    loops_.pop_back();
    if (!currentBlockTerminated()) {
      terminate("br " + condLabel);
    }

    startBlock(endLabel);
    return;
  }

  if (dynamic_cast<const BreakStmt *>(&stmt)) {
    terminate("br " + loops_.back().breakLabel);
    return;
  }

  if (dynamic_cast<const ContinueStmt *>(&stmt)) {
    terminate("br " + loops_.back().continueLabel);
    return;
  }

  if (const auto *ret = dynamic_cast<const ReturnStmt *>(&stmt)) {
    if (ret->value) {
      terminate("ret " + genExpr(*ret->value));
    } else {
      terminate("ret");
    }
  }
}

std::string IRGenerator::genExpr(const Expr &expr) {
  if (const auto *integer = dynamic_cast<const IntExpr *>(&expr)) {
    return std::to_string(integer->value);
  }

  if (const auto *floating = dynamic_cast<const FloatExpr *>(&expr)) {
    std::ostringstream os;
    os << std::setprecision(9) << floating->value;
    return os.str();
  }

  if (const auto *var = dynamic_cast<const VarExpr *>(&expr)) {
    std::string addr = genLValue(*var);
    const Binding *binding = lookup(var->name);
    if (binding && binding->isArray && var->indices.empty()) {
      return addr;
    }

    std::string tmp = freshTemp();
    emit(tmp + " = load " + addr);
    return tmp;
  }

  if (const auto *unary = dynamic_cast<const UnaryExpr *>(&expr)) {
    std::string value = genExpr(*unary->operand);
    if (unary->op == "+") {
      return value;
    }
    std::string tmp = freshTemp();
    if (unary->op == "-") {
      emit(tmp + " = neg " + value);
    } else {
      emit(tmp + " = eq " + value + ", 0");
    }
    return tmp;
  }

  if (const auto *binary = dynamic_cast<const BinaryExpr *>(&expr)) {
    if (binary->op == "&&" || binary->op == "||") {
      return emitShortCircuitValue(*binary);
    }
    return emitBinary(*binary);
  }

  if (const auto *call = dynamic_cast<const CallExpr *>(&expr)) {
    std::string args;
    for (std::size_t i = 0; i < call->args.size(); ++i) {
      if (i != 0) {
        args += ", ";
      }
      args += genExpr(*call->args[i]);
    }

    std::string retType = functionReturnType(call->callee);
    if (isVoidType(retType)) {
      emit("call @" + call->callee + "(" + args + ")");
      return "void";
    }

    std::string tmp = freshTemp();
    emit(tmp + " = call @" + call->callee + "(" + args + ")");
    return tmp;
  }

  return "undef";
}

std::string IRGenerator::genLValue(const VarExpr &expr) {
  const Binding *binding = lookup(expr.name);
  std::string base = binding ? binding->address : "@" + expr.name;
  if (expr.indices.empty()) {
    return base;
  }

  std::string indices;
  for (std::size_t i = 0; i < expr.indices.size(); ++i) {
    if (i != 0) {
      indices += ", ";
    }
    indices += genExpr(*expr.indices[i]);
  }

  std::string tmp = freshTemp();
  emit(tmp + " = gep " + base + ", " + indices);
  return tmp;
}

void IRGenerator::genCond(const Expr &expr, const std::string &trueLabel,
                          const std::string &falseLabel) {
  if (const auto *binary = dynamic_cast<const BinaryExpr *>(&expr)) {
    if (binary->op == "&&") {
      std::string rhsLabel = freshLabel("land.rhs");
      genCond(*binary->lhs, rhsLabel, falseLabel);
      startBlock(rhsLabel);
      genCond(*binary->rhs, trueLabel, falseLabel);
      return;
    }

    if (binary->op == "||") {
      std::string rhsLabel = freshLabel("lor.rhs");
      genCond(*binary->lhs, trueLabel, rhsLabel);
      startBlock(rhsLabel);
      genCond(*binary->rhs, trueLabel, falseLabel);
      return;
    }
  }

  std::string value = genExpr(expr);
  terminate("br " + value + ", " + trueLabel + ", " + falseLabel);
}

std::string IRGenerator::emitBinary(const BinaryExpr &expr) {
  std::string lhs = genExpr(*expr.lhs);
  std::string rhs = genExpr(*expr.rhs);
  std::string tmp = freshTemp();

  std::string op = expr.op;
  if (op == "+") {
    op = "add";
  } else if (op == "-") {
    op = "sub";
  } else if (op == "*") {
    op = "mul";
  } else if (op == "/") {
    op = "div";
  } else if (op == "%") {
    op = "rem";
  } else if (op == "==") {
    op = "eq";
  } else if (op == "!=") {
    op = "ne";
  } else if (op == "<") {
    op = "lt";
  } else if (op == "<=") {
    op = "le";
  } else if (op == ">") {
    op = "gt";
  } else if (op == ">=") {
    op = "ge";
  }

  emit(tmp + " = " + op + " " + lhs + ", " + rhs);
  return tmp;
}

std::string IRGenerator::emitShortCircuitValue(const BinaryExpr &expr) {
  std::string slot = freshTemp() + ".bool.addr";
  emit(slot + " = alloca i32");

  std::string trueLabel = freshLabel("logic.true");
  std::string falseLabel = freshLabel("logic.false");
  std::string endLabel = freshLabel("logic.end");
  genCond(expr, trueLabel, falseLabel);

  startBlock(trueLabel);
  emit("store 1, " + slot);
  terminate("br " + endLabel);

  startBlock(falseLabel);
  emit("store 0, " + slot);
  terminate("br " + endLabel);

  startBlock(endLabel);
  std::string value = freshTemp();
  emit(value + " = load " + slot);
  return value;
}

std::string IRGenerator::renderInit(const InitVal *init) const {
  if (!init) {
    return "zeroinit";
  }

  if (!init->isList) {
    return init->expr ? renderExprPreview(*init->expr) : "zeroinit";
  }

  std::string result = "{";
  for (std::size_t i = 0; i < init->values.size(); ++i) {
    if (i != 0) {
      result += ", ";
    }
    result += renderInit(init->values[i].get());
  }
  result += "}";
  return result;
}

std::string IRGenerator::renderType(const TypeSpec &type, bool isArray,
                                    const std::vector<ExprPtr> *dims) const {
  std::string result = baseType(type.base);
  if (!isArray) {
    return result;
  }

  if (!dims || dims->empty()) {
    return "ptr " + result;
  }

  for (auto it = dims->rbegin(); it != dims->rend(); ++it) {
    result = "[" + renderExprPreview(**it) + " x " + result + "]";
  }
  return result;
}

std::string IRGenerator::renderExprPreview(const Expr &expr) const {
  if (const auto *integer = dynamic_cast<const IntExpr *>(&expr)) {
    return std::to_string(integer->value);
  }
  if (const auto *floating = dynamic_cast<const FloatExpr *>(&expr)) {
    std::ostringstream os;
    os << std::setprecision(9) << floating->value;
    return os.str();
  }
  if (const auto *var = dynamic_cast<const VarExpr *>(&expr)) {
    return "%" + var->name;
  }
  if (const auto *unary = dynamic_cast<const UnaryExpr *>(&expr)) {
    return "(" + unary->op + renderExprPreview(*unary->operand) + ")";
  }
  if (const auto *binary = dynamic_cast<const BinaryExpr *>(&expr)) {
    return "(" + renderExprPreview(*binary->lhs) + " " + binary->op + " " +
           renderExprPreview(*binary->rhs) + ")";
  }
  if (const auto *call = dynamic_cast<const CallExpr *>(&expr)) {
    return "call @" + call->callee;
  }
  return "?";
}

std::string IRGenerator::functionReturnType(const std::string &name) const {
  auto found = functionReturns_.find(name);
  if (found != functionReturns_.end()) {
    return found->second;
  }
  return "i32";
}

void IRGenerator::pushScope() { scopes_.emplace_back(); }

void IRGenerator::popScope() { scopes_.pop_back(); }

void IRGenerator::bind(const std::string &name, Binding binding) {
  scopes_.back()[name] = std::move(binding);
}

const IRGenerator::Binding *IRGenerator::lookup(const std::string &name) const {
  for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
    auto found = it->find(name);
    if (found != it->end()) {
      return &found->second;
    }
  }

  auto found = globals_.find(name);
  if (found != globals_.end()) {
    return &found->second;
  }
  return nullptr;
}

std::string IRGenerator::freshTemp() {
  return "%t" + std::to_string(tempId_++);
}

std::string IRGenerator::freshLabel(const std::string &base) {
  return base + "." + std::to_string(labelId_++);
}

std::string IRGenerator::freshLocalName(const std::string &base) {
  return valueName(base) + "." + std::to_string(localId_++);
}

std::string IRGenerator::valueName(const std::string &base) const {
  std::string result;
  result.reserve(base.size());
  for (char c : base) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '.') {
      result.push_back(c);
    } else {
      result.push_back('_');
    }
  }
  if (result.empty()) {
    return "v";
  }
  return result;
}

void IRGenerator::startBlock(const std::string &label) {
  if (!currentFunction_) {
    throw std::logic_error("cannot start IR block without a function");
  }
  currentFunction_->blocks.push_back(BasicBlock{label, {}, false});
  currentBlock_ = &currentFunction_->blocks.back();
}

void IRGenerator::emit(const std::string &text) {
  if (!currentBlock_) {
    throw std::logic_error("cannot emit IR without a block");
  }
  currentBlock_->instructions.push_back(text);
}

void IRGenerator::terminate(const std::string &text) {
  if (!currentBlockTerminated()) {
    emit(text);
    currentBlock_->terminated = true;
  }
}

bool IRGenerator::currentBlockTerminated() const {
  return currentBlock_ && currentBlock_->terminated;
}

} // namespace by::ir
