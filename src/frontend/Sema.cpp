#include "Sema.h"

#include <utility>

namespace by {

std::string ValueType::str() const {
  std::string result;
  if (isConst) {
    result += "const ";
  }
  switch (base) {
  case BuiltinType::Int:
    result += "int";
    break;
  case BuiltinType::Float:
    result += "float";
    break;
  case BuiltinType::Void:
    result += "void";
    break;
  case BuiltinType::Unknown:
    result += "<unknown>";
    break;
  }
  if (isArray) {
    result += "[]";
  }
  return result;
}

SemanticAnalyzer::SemanticAnalyzer(DiagnosticEngine &diag) : diag_(diag) {}

bool SemanticAnalyzer::analyze(CompUnit &unit) {
  scopes_.clear();
  pushScope();
  installBuiltins();

  for (auto &decl : unit.decls) {
    if (auto *func = dynamic_cast<FuncDef *>(decl.get())) {
      predeclareFunction(*func);
    }
  }

  for (auto &decl : unit.decls) {
    checkTopLevelDecl(*decl);
  }

  Symbol *main = lookup("main");
  if (!main || main->kind != Symbol::Kind::Function) {
    diag_.error(unit.loc, "missing main function");
  }

  return !diag_.hasErrors();
}

void SemanticAnalyzer::pushScope() { scopes_.emplace_back(); }

void SemanticAnalyzer::popScope() { scopes_.pop_back(); }

Symbol *SemanticAnalyzer::lookup(const std::string &name) {
  for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
    auto found = it->find(name);
    if (found != it->end()) {
      return &found->second;
    }
  }
  return nullptr;
}

Symbol *SemanticAnalyzer::lookupCurrent(const std::string &name) {
  if (scopes_.empty()) {
    return nullptr;
  }
  auto found = scopes_.back().find(name);
  if (found == scopes_.back().end()) {
    return nullptr;
  }
  return &found->second;
}

bool SemanticAnalyzer::declare(const std::string &name, const Symbol &symbol,
                               const SourceLocation &loc) {
  if (lookupCurrent(name)) {
    diag_.error(loc, "redefinition of '" + name + "'");
    return false;
  }
  scopes_.back().emplace(name, symbol);
  return true;
}

void SemanticAnalyzer::installBuiltins() {
  auto fn = [&](const std::string &name, BuiltinType ret,
                std::vector<ValueType> params) {
    Symbol sym;
    sym.kind = Symbol::Kind::Function;
    sym.type = scalar(ret);
    sym.params = std::move(params);
    scopes_.back().emplace(name, std::move(sym));
  };

  fn("getint", BuiltinType::Int, {});
  fn("getch", BuiltinType::Int, {});
  fn("getarray", BuiltinType::Int, {arrayOf(BuiltinType::Int)});
  fn("getfloat", BuiltinType::Float, {});
  fn("getfarray", BuiltinType::Int, {arrayOf(BuiltinType::Float)});
  fn("putint", BuiltinType::Void, {scalar(BuiltinType::Int)});
  fn("putch", BuiltinType::Void, {scalar(BuiltinType::Int)});
  fn("putarray", BuiltinType::Void,
     {scalar(BuiltinType::Int), arrayOf(BuiltinType::Int)});
  fn("putfloat", BuiltinType::Void, {scalar(BuiltinType::Float)});
  fn("putfarray", BuiltinType::Void,
     {scalar(BuiltinType::Int), arrayOf(BuiltinType::Float)});
  fn("starttime", BuiltinType::Void, {});
  fn("stoptime", BuiltinType::Void, {});
}

void SemanticAnalyzer::predeclareFunction(FuncDef &func) {
  Symbol sym;
  sym.kind = Symbol::Kind::Function;
  sym.type = scalar(func.returnType.base);
  for (const auto &param : func.params) {
    ValueType type = param->isArray ? arrayOf(param->type.base)
                                    : scalar(param->type.base);
    sym.params.push_back(type);
  }
  declare(func.name, sym, func.loc);
}

void SemanticAnalyzer::checkTopLevelDecl(Decl &decl) {
  if (auto *var = dynamic_cast<VarDecl *>(&decl)) {
    checkVarDecl(*var, true);
  } else if (auto *func = dynamic_cast<FuncDef *>(&decl)) {
    checkFuncDef(*func);
  }
}

void SemanticAnalyzer::checkVarDecl(VarDecl &decl, bool global) {
  (void)global;
  if (decl.type.base == BuiltinType::Void) {
    diag_.error(decl.loc, "variable cannot have type void");
    return;
  }

  for (auto &def : decl.defs) {
    Symbol sym;
    sym.kind = Symbol::Kind::Variable;
    sym.type = scalar(decl.type.base, decl.type.isConst);
    sym.type.isArray = !def->dimensions.empty();

    for (auto &dim : def->dimensions) {
      ValueType dimType = checkExpr(*dim);
      if (dimType.base != BuiltinType::Int || dimType.isArray) {
        diag_.error(dim->loc, "array dimension must be an integer expression");
      }
      auto value = evalConstInt(*dim);
      if (!value) {
        diag_.error(dim->loc, "array dimension must be compile-time constant");
      } else if (*value <= 0) {
        diag_.error(dim->loc, "array dimension must be positive");
      } else {
        sym.dimensions.push_back(*value);
      }
    }

    if (def->init) {
      ValueType initType = checkInitVal(*def->init);
      if (!sym.type.isArray && !isAssignable(sym.type, initType)) {
        diag_.error(def->init->loc, "initializer type does not match variable");
      }
      if (decl.type.isConst && !sym.type.isArray && def->init->expr) {
        sym.constInt = evalConstInt(*def->init->expr);
      }
    } else if (decl.type.isConst) {
      diag_.error(def->loc, "const variable requires an initializer");
    }

    declare(def->name, sym, def->loc);
  }
}

void SemanticAnalyzer::checkFuncDef(FuncDef &func) {
  currentReturn_ = func.returnType.base;
  pushScope();

  for (auto &param : func.params) {
    Symbol sym;
    sym.kind = Symbol::Kind::Variable;
    sym.type = param->isArray ? arrayOf(param->type.base)
                              : scalar(param->type.base);
    for (auto &dim : param->dimensions) {
      checkExpr(*dim);
      auto value = evalConstInt(*dim);
      if (value) {
        sym.dimensions.push_back(*value);
      }
    }
    declare(param->name, sym, param->loc);
  }

  if (func.body) {
    checkBlock(*func.body, false);
  }

  popScope();
  currentReturn_ = BuiltinType::Unknown;
}

void SemanticAnalyzer::checkBlock(BlockStmt &block, bool createsScope) {
  if (createsScope) {
    pushScope();
  }
  for (auto &item : block.items) {
    if (item.decl) {
      if (auto *var = dynamic_cast<VarDecl *>(item.decl.get())) {
        checkVarDecl(*var, false);
      }
    } else if (item.stmt) {
      checkStmt(*item.stmt);
    }
  }
  if (createsScope) {
    popScope();
  }
}

void SemanticAnalyzer::checkStmt(Stmt &stmt) {
  if (auto *block = dynamic_cast<BlockStmt *>(&stmt)) {
    checkBlock(*block);
    return;
  }

  if (auto *expr = dynamic_cast<ExprStmt *>(&stmt)) {
    if (expr->expr) {
      checkExpr(*expr->expr);
    }
    return;
  }

  if (auto *assign = dynamic_cast<AssignStmt *>(&stmt)) {
    Symbol *sym = lookup(assign->target->name);
    if (sym && sym->type.isConst) {
      diag_.error(assign->target->loc,
                  "cannot assign to const variable '" + assign->target->name +
                      "'");
    }
    ValueType lhs = checkExpr(*assign->target);
    ValueType rhs = checkExpr(*assign->value);
    if (lhs.isArray) {
      diag_.error(assign->target->loc, "cannot assign to an array value");
    } else if (!isAssignable(lhs, rhs)) {
      diag_.error(assign->loc, "assignment type mismatch");
    }
    return;
  }

  if (auto *ifs = dynamic_cast<IfStmt *>(&stmt)) {
    checkExpr(*ifs->cond);
    checkStmt(*ifs->thenBranch);
    if (ifs->elseBranch) {
      checkStmt(*ifs->elseBranch);
    }
    return;
  }

  if (auto *whileStmt = dynamic_cast<WhileStmt *>(&stmt)) {
    checkExpr(*whileStmt->cond);
    ++loopDepth_;
    checkStmt(*whileStmt->body);
    --loopDepth_;
    return;
  }

  if (dynamic_cast<BreakStmt *>(&stmt)) {
    if (loopDepth_ == 0) {
      diag_.error(stmt.loc, "break statement is not inside a loop");
    }
    return;
  }

  if (dynamic_cast<ContinueStmt *>(&stmt)) {
    if (loopDepth_ == 0) {
      diag_.error(stmt.loc, "continue statement is not inside a loop");
    }
    return;
  }

  if (auto *ret = dynamic_cast<ReturnStmt *>(&stmt)) {
    if (currentReturn_ == BuiltinType::Void) {
      if (ret->value) {
        diag_.error(ret->loc, "void function should not return a value");
      }
    } else {
      if (!ret->value) {
        diag_.error(ret->loc, "non-void function must return a value");
      } else {
        ValueType value = checkExpr(*ret->value);
        if (!isAssignable(scalar(currentReturn_), value)) {
          diag_.error(ret->loc, "return type mismatch");
        }
      }
    }
  }
}

ValueType SemanticAnalyzer::checkExpr(Expr &expr) {
  if (dynamic_cast<IntExpr *>(&expr)) {
    return scalar(BuiltinType::Int);
  }
  if (dynamic_cast<FloatExpr *>(&expr)) {
    return scalar(BuiltinType::Float);
  }

  if (auto *var = dynamic_cast<VarExpr *>(&expr)) {
    Symbol *sym = lookup(var->name);
    if (!sym) {
      diag_.error(var->loc, "undefined identifier '" + var->name + "'");
      return scalar(BuiltinType::Unknown);
    }
    if (sym->kind != Symbol::Kind::Variable) {
      diag_.error(var->loc, "'" + var->name + "' is not a variable");
      return scalar(BuiltinType::Unknown);
    }
    for (auto &index : var->indices) {
      ValueType idx = checkExpr(*index);
      if (idx.base != BuiltinType::Int || idx.isArray) {
        diag_.error(index->loc, "array subscript must be an integer");
      }
    }
    ValueType result = sym->type;
    if (!sym->dimensions.empty()) {
      result.isArray = var->indices.size() < sym->dimensions.size();
    } else if (sym->type.isArray) {
      result.isArray = var->indices.empty();
    }
    return result;
  }

  if (auto *unary = dynamic_cast<UnaryExpr *>(&expr)) {
    ValueType operand = checkExpr(*unary->operand);
    if (!isNumeric(operand)) {
      diag_.error(unary->loc, "unary operator requires numeric operand");
      return scalar(BuiltinType::Unknown);
    }
    if (unary->op == "!") {
      return scalar(BuiltinType::Int);
    }
    return scalar(operand.base);
  }

  if (auto *binary = dynamic_cast<BinaryExpr *>(&expr)) {
    ValueType lhs = checkExpr(*binary->lhs);
    ValueType rhs = checkExpr(*binary->rhs);
    if (!isNumeric(lhs) || !isNumeric(rhs)) {
      diag_.error(binary->loc, "binary operator requires numeric operands");
      return scalar(BuiltinType::Unknown);
    }

    if (binary->op == "&&" || binary->op == "||" || binary->op == "==" ||
        binary->op == "!=" || binary->op == "<" || binary->op == "<=" ||
        binary->op == ">" || binary->op == ">=") {
      return scalar(BuiltinType::Int);
    }

    if (binary->op == "%" &&
        (lhs.base != BuiltinType::Int || rhs.base != BuiltinType::Int)) {
      diag_.error(binary->loc, "remainder operator requires integer operands");
      return scalar(BuiltinType::Int);
    }

    if (lhs.base == BuiltinType::Float || rhs.base == BuiltinType::Float) {
      return scalar(BuiltinType::Float);
    }
    return scalar(BuiltinType::Int);
  }

  if (auto *call = dynamic_cast<CallExpr *>(&expr)) {
    Symbol *sym = lookup(call->callee);
    if (!sym) {
      diag_.error(call->loc, "undefined function '" + call->callee + "'");
      return scalar(BuiltinType::Unknown);
    }
    if (sym->kind != Symbol::Kind::Function) {
      diag_.error(call->loc, "'" + call->callee + "' is not a function");
      return scalar(BuiltinType::Unknown);
    }
    if (call->args.size() != sym->params.size()) {
      diag_.error(call->loc, "function '" + call->callee +
                                 "' called with wrong number of arguments");
    }
    std::size_t n =
        call->args.size() < sym->params.size() ? call->args.size()
                                               : sym->params.size();
    for (std::size_t i = 0; i < n; ++i) {
      ValueType arg = checkExpr(*call->args[i]);
      if (!isAssignable(sym->params[i], arg)) {
        diag_.error(call->args[i]->loc, "function argument type mismatch");
      }
    }
    return sym->type;
  }

  return scalar(BuiltinType::Unknown);
}

ValueType SemanticAnalyzer::checkInitVal(InitVal &init) {
  if (init.isList) {
    for (auto &value : init.values) {
      checkInitVal(*value);
    }
    return arrayOf(BuiltinType::Unknown);
  }
  if (!init.expr) {
    return scalar(BuiltinType::Unknown);
  }
  return checkExpr(*init.expr);
}

std::optional<long long> SemanticAnalyzer::evalConstInt(Expr &expr) {
  if (auto *integer = dynamic_cast<IntExpr *>(&expr)) {
    return integer->value;
  }
  if (auto *var = dynamic_cast<VarExpr *>(&expr)) {
    if (!var->indices.empty()) {
      return std::nullopt;
    }
    Symbol *sym = lookup(var->name);
    if (sym && sym->constInt) {
      return sym->constInt;
    }
    return std::nullopt;
  }
  if (auto *unary = dynamic_cast<UnaryExpr *>(&expr)) {
    auto value = evalConstInt(*unary->operand);
    if (!value) {
      return std::nullopt;
    }
    if (unary->op == "+") {
      return *value;
    }
    if (unary->op == "-") {
      return -*value;
    }
    if (unary->op == "!") {
      return *value == 0 ? 1 : 0;
    }
    return std::nullopt;
  }
  if (auto *binary = dynamic_cast<BinaryExpr *>(&expr)) {
    auto lhs = evalConstInt(*binary->lhs);
    auto rhs = evalConstInt(*binary->rhs);
    if (!lhs || !rhs) {
      return std::nullopt;
    }
    if (binary->op == "+") {
      return *lhs + *rhs;
    }
    if (binary->op == "-") {
      return *lhs - *rhs;
    }
    if (binary->op == "*") {
      return *lhs * *rhs;
    }
    if (binary->op == "/" && *rhs != 0) {
      return *lhs / *rhs;
    }
    if (binary->op == "%" && *rhs != 0) {
      return *lhs % *rhs;
    }
    if (binary->op == "==") {
      return *lhs == *rhs;
    }
    if (binary->op == "!=") {
      return *lhs != *rhs;
    }
    if (binary->op == "<") {
      return *lhs < *rhs;
    }
    if (binary->op == "<=") {
      return *lhs <= *rhs;
    }
    if (binary->op == ">") {
      return *lhs > *rhs;
    }
    if (binary->op == ">=") {
      return *lhs >= *rhs;
    }
    if (binary->op == "&&") {
      return (*lhs != 0) && (*rhs != 0);
    }
    if (binary->op == "||") {
      return (*lhs != 0) || (*rhs != 0);
    }
  }
  return std::nullopt;
}

ValueType SemanticAnalyzer::scalar(BuiltinType base, bool isConst) const {
  return ValueType{base, false, isConst};
}

ValueType SemanticAnalyzer::arrayOf(BuiltinType base, bool isConst) const {
  return ValueType{base, true, isConst};
}

bool SemanticAnalyzer::isNumeric(ValueType type) const {
  return !type.isArray &&
         (type.base == BuiltinType::Int || type.base == BuiltinType::Float);
}

bool SemanticAnalyzer::isAssignable(ValueType target, ValueType value) const {
  if (target.base == BuiltinType::Unknown || value.base == BuiltinType::Unknown) {
    return true;
  }
  if (target.isArray || value.isArray) {
    return target.isArray == value.isArray &&
           (target.base == value.base || target.base == BuiltinType::Unknown ||
            value.base == BuiltinType::Unknown);
  }
  if ((target.base == BuiltinType::Float || target.base == BuiltinType::Int) &&
      (value.base == BuiltinType::Float || value.base == BuiltinType::Int)) {
    return true;
  }
  return target.base == value.base;
}

} // namespace by
