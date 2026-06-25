#include "../../include/frontend/ASTOptimizer.h"

#include <optional>
#include <utility>

namespace by {

namespace {

struct ConstValue {
  BuiltinType type = BuiltinType::Int;
  long long intValue = 0;
  double floatValue = 0.0;
};

bool truthy(const ConstValue &value) {
  return value.type == BuiltinType::Float ? value.floatValue != 0.0
                                          : value.intValue != 0;
}

ConstValue intConst(long long value) {
  return ConstValue{BuiltinType::Int, value, static_cast<double>(value)};
}

ConstValue floatConst(double value) {
  return ConstValue{BuiltinType::Float, static_cast<long long>(value), value};
}

std::optional<ConstValue> evalConst(const Expr &expr) {
  if (const auto *integer = dynamic_cast<const IntExpr *>(&expr)) {
    return intConst(integer->value);
  }
  if (const auto *floating = dynamic_cast<const FloatExpr *>(&expr)) {
    return floatConst(floating->value);
  }
  if (const auto *unary = dynamic_cast<const UnaryExpr *>(&expr)) {
    auto value = evalConst(*unary->operand);
    if (!value) {
      return std::nullopt;
    }
    if (unary->op == "+") {
      return value;
    }
    if (unary->op == "-") {
      return value->type == BuiltinType::Float ? floatConst(-value->floatValue)
                                               : intConst(-value->intValue);
    }
    if (unary->op == "!") {
      return intConst(truthy(*value) ? 0 : 1);
    }
    return std::nullopt;
  }
  if (const auto *binary = dynamic_cast<const BinaryExpr *>(&expr)) {
    auto lhs = evalConst(*binary->lhs);
    auto rhs = evalConst(*binary->rhs);
    if (!lhs || !rhs) {
      return std::nullopt;
    }

    bool useFloat =
        lhs->type == BuiltinType::Float || rhs->type == BuiltinType::Float;
    double lf = lhs->type == BuiltinType::Float
                    ? lhs->floatValue
                    : static_cast<double>(lhs->intValue);
    double rf = rhs->type == BuiltinType::Float
                    ? rhs->floatValue
                    : static_cast<double>(rhs->intValue);
    long long li = lhs->intValue;
    long long ri = rhs->intValue;

    if (binary->op == "&&") return intConst(truthy(*lhs) && truthy(*rhs));
    if (binary->op == "||") return intConst(truthy(*lhs) || truthy(*rhs));
    if (binary->op == "==") return intConst(useFloat ? lf == rf : li == ri);
    if (binary->op == "!=") return intConst(useFloat ? lf != rf : li != ri);
    if (binary->op == "<") return intConst(useFloat ? lf < rf : li < ri);
    if (binary->op == "<=") return intConst(useFloat ? lf <= rf : li <= ri);
    if (binary->op == ">") return intConst(useFloat ? lf > rf : li > ri);
    if (binary->op == ">=") return intConst(useFloat ? lf >= rf : li >= ri);

    if (useFloat) {
      if (binary->op == "+") return floatConst(lf + rf);
      if (binary->op == "-") return floatConst(lf - rf);
      if (binary->op == "*") return floatConst(lf * rf);
      if (binary->op == "/" && rf != 0.0) return floatConst(lf / rf);
      return std::nullopt;
    }

    if (binary->op == "+") return intConst(li + ri);
    if (binary->op == "-") return intConst(li - ri);
    if (binary->op == "*") return intConst(li * ri);
    if (binary->op == "/" && ri != 0) return intConst(li / ri);
    if (binary->op == "%" && ri != 0) return intConst(li % ri);
  }
  return std::nullopt;
}

ExprPtr makeConstExpr(SourceLocation loc, const ConstValue &value) {
  if (value.type == BuiltinType::Float) {
    return std::make_unique<FloatExpr>(std::move(loc), value.floatValue);
  }
  return std::make_unique<IntExpr>(std::move(loc), value.intValue);
}

bool hasSideEffects(const Expr &expr) {
  if (dynamic_cast<const CallExpr *>(&expr)) {
    return true;
  }
  if (const auto *unary = dynamic_cast<const UnaryExpr *>(&expr)) {
    return hasSideEffects(*unary->operand);
  }
  if (const auto *binary = dynamic_cast<const BinaryExpr *>(&expr)) {
    return hasSideEffects(*binary->lhs) || hasSideEffects(*binary->rhs);
  }
  if (const auto *var = dynamic_cast<const VarExpr *>(&expr)) {
    for (const auto &index : var->indices) {
      if (hasSideEffects(*index)) {
        return true;
      }
    }
  }
  return false;
}

class Optimizer {
public:
  void optimize(CompUnit &unit) {
    for (auto &decl : unit.decls) {
      optimizeDecl(*decl);
    }
  }

private:
  void optimizeDecl(Decl &decl) {
    if (auto *var = dynamic_cast<VarDecl *>(&decl)) {
      optimizeVarDecl(*var);
      return;
    }
    if (auto *func = dynamic_cast<FuncDef *>(&decl)) {
      for (auto &param : func->params) {
        for (auto &dim : param->dimensions) {
          dim = optimizeExpr(std::move(dim));
        }
      }
      if (func->body) {
        optimizeBlock(*func->body);
      }
    }
  }

  void optimizeVarDecl(VarDecl &decl) {
    for (auto &def : decl.defs) {
      for (auto &dim : def->dimensions) {
        dim = optimizeExpr(std::move(dim));
      }
      if (def->init) {
        optimizeInit(*def->init);
      }
    }
  }

  void optimizeInit(InitVal &init) {
    if (init.isList) {
      for (auto &value : init.values) {
        optimizeInit(*value);
      }
      return;
    }
    if (init.expr) {
      init.expr = optimizeExpr(std::move(init.expr));
    }
  }

  void optimizeBlock(BlockStmt &block) {
    std::vector<BlockItem> optimized;
    optimized.reserve(block.items.size());
    bool unreachable = false;
    for (auto &item : block.items) {
      if (unreachable) {
        continue;
      }
      if (item.decl) {
        if (auto *var = dynamic_cast<VarDecl *>(item.decl.get())) {
          optimizeVarDecl(*var);
        }
        optimized.push_back(std::move(item));
        continue;
      }
      if (item.stmt) {
        item.stmt = optimizeStmt(std::move(item.stmt));
        unreachable = isTerminating(*item.stmt);
        if (!isEmptyStmt(*item.stmt)) {
          optimized.push_back(std::move(item));
        }
      }
    }
    block.items = std::move(optimized);
  }

  StmtPtr optimizeStmt(StmtPtr stmt) {
    if (auto *block = dynamic_cast<BlockStmt *>(stmt.get())) {
      optimizeBlock(*block);
      return stmt;
    }
    if (auto *exprStmt = dynamic_cast<ExprStmt *>(stmt.get())) {
      if (exprStmt->expr) {
        exprStmt->expr = optimizeExpr(std::move(exprStmt->expr));
        if (!hasSideEffects(*exprStmt->expr)) {
          return std::make_unique<EmptyStmt>(exprStmt->loc);
        }
      }
      return stmt;
    }
    if (auto *assign = dynamic_cast<AssignStmt *>(stmt.get())) {
      for (auto &index : assign->target->indices) {
        index = optimizeExpr(std::move(index));
      }
      assign->value = optimizeExpr(std::move(assign->value));
      return stmt;
    }
    if (auto *ifs = dynamic_cast<IfStmt *>(stmt.get())) {
      ifs->cond = optimizeExpr(std::move(ifs->cond));
      ifs->thenBranch = optimizeStmt(std::move(ifs->thenBranch));
      if (ifs->elseBranch) {
        ifs->elseBranch = optimizeStmt(std::move(ifs->elseBranch));
      }
      if (auto cond = evalConst(*ifs->cond)) {
        if (truthy(*cond)) {
          return std::move(ifs->thenBranch);
        }
        if (ifs->elseBranch) {
          return std::move(ifs->elseBranch);
        }
        return std::make_unique<EmptyStmt>(ifs->loc);
      }
      return stmt;
    }
    if (auto *whileStmt = dynamic_cast<WhileStmt *>(stmt.get())) {
      whileStmt->cond = optimizeExpr(std::move(whileStmt->cond));
      whileStmt->body = optimizeStmt(std::move(whileStmt->body));
      if (auto cond = evalConst(*whileStmt->cond); cond && !truthy(*cond)) {
        return std::make_unique<EmptyStmt>(whileStmt->loc);
      }
      return stmt;
    }
    if (auto *ret = dynamic_cast<ReturnStmt *>(stmt.get())) {
      if (ret->value) {
        ret->value = optimizeExpr(std::move(ret->value));
      }
      return stmt;
    }
    return stmt;
  }

  ExprPtr optimizeExpr(ExprPtr expr) {
    if (auto *var = dynamic_cast<VarExpr *>(expr.get())) {
      for (auto &index : var->indices) {
        index = optimizeExpr(std::move(index));
      }
    } else if (auto *unary = dynamic_cast<UnaryExpr *>(expr.get())) {
      unary->operand = optimizeExpr(std::move(unary->operand));
    } else if (auto *binary = dynamic_cast<BinaryExpr *>(expr.get())) {
      binary->lhs = optimizeExpr(std::move(binary->lhs));
      binary->rhs = optimizeExpr(std::move(binary->rhs));
    } else if (auto *call = dynamic_cast<CallExpr *>(expr.get())) {
      for (auto &arg : call->args) {
        arg = optimizeExpr(std::move(arg));
      }
    }

    if (auto value = evalConst(*expr)) {
      return makeConstExpr(expr->loc, *value);
    }
    return expr;
  }

  bool isEmptyStmt(const Stmt &stmt) const {
    return dynamic_cast<const EmptyStmt *>(&stmt) != nullptr;
  }

  bool isTerminating(const Stmt &stmt) const {
    if (dynamic_cast<const ReturnStmt *>(&stmt) ||
        dynamic_cast<const BreakStmt *>(&stmt) ||
        dynamic_cast<const ContinueStmt *>(&stmt)) {
      return true;
    }
    if (const auto *block = dynamic_cast<const BlockStmt *>(&stmt)) {
      return !block->items.empty() && block->items.back().stmt &&
             isTerminating(*block->items.back().stmt);
    }
    if (const auto *ifs = dynamic_cast<const IfStmt *>(&stmt)) {
      return ifs->elseBranch && isTerminating(*ifs->thenBranch) &&
             isTerminating(*ifs->elseBranch);
    }
    return false;
  }
};

} // namespace

void ASTOptimizer::optimize(CompUnit &unit) { Optimizer().optimize(unit); }

} // namespace by
