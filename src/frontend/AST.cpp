#include "frontend/AST.h"

#include <iostream>
#include <utility>

namespace by {

namespace {

void line(std::ostream &os, int indent, const std::string &text) {
  dumpIndent(os, indent);
  os << text << '\n';
}

std::string quote(const std::string &value) { return "'" + value + "'"; }

} // namespace

std::string TypeSpec::str() const {
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
  return result;
}

void dumpIndent(std::ostream &os, int indent) {
  for (int i = 0; i < indent; ++i) {
    os << "  ";
  }
}

IntExpr::IntExpr(SourceLocation loc, long long value)
    : Expr(std::move(loc)), value(value) {}

void IntExpr::dump(std::ostream &os, int indent) const {
  line(os, indent, "IntLiteral " + std::to_string(value));
}

FloatExpr::FloatExpr(SourceLocation loc, double value)
    : Expr(std::move(loc)), value(value) {}

void FloatExpr::dump(std::ostream &os, int indent) const {
  line(os, indent, "FloatLiteral " + std::to_string(value));
}

VarExpr::VarExpr(SourceLocation loc, std::string name)
    : Expr(std::move(loc)), name(std::move(name)) {}

void VarExpr::dump(std::ostream &os, int indent) const {
  line(os, indent, "VarRef " + quote(name));
  for (const auto &index : indices) {
    line(os, indent + 1, "Index");
    index->dump(os, indent + 2);
  }
}

UnaryExpr::UnaryExpr(SourceLocation loc, std::string op, ExprPtr operand)
    : Expr(std::move(loc)), op(std::move(op)), operand(std::move(operand)) {}

void UnaryExpr::dump(std::ostream &os, int indent) const {
  line(os, indent, "Unary " + quote(op));
  operand->dump(os, indent + 1);
}

BinaryExpr::BinaryExpr(SourceLocation loc, std::string op, ExprPtr lhs,
                       ExprPtr rhs)
    : Expr(std::move(loc)), op(std::move(op)), lhs(std::move(lhs)),
      rhs(std::move(rhs)) {}

void BinaryExpr::dump(std::ostream &os, int indent) const {
  line(os, indent, "Binary " + quote(op));
  lhs->dump(os, indent + 1);
  rhs->dump(os, indent + 1);
}

CallExpr::CallExpr(SourceLocation loc, std::string callee)
    : Expr(std::move(loc)), callee(std::move(callee)) {}

void CallExpr::dump(std::ostream &os, int indent) const {
  line(os, indent, "Call " + quote(callee));
  for (const auto &arg : args) {
    arg->dump(os, indent + 1);
  }
}

InitVal::InitVal(SourceLocation loc) : Node(std::move(loc)) {}

void InitVal::dump(std::ostream &os, int indent) const {
  if (isList) {
    line(os, indent, "InitList");
    for (const auto &value : values) {
      value->dump(os, indent + 1);
    }
  } else {
    line(os, indent, "InitExpr");
    if (expr) {
      expr->dump(os, indent + 1);
    }
  }
}

void BlockItem::dump(std::ostream &os, int indent) const {
  if (decl) {
    decl->dump(os, indent);
  } else if (stmt) {
    stmt->dump(os, indent);
  }
}

BlockStmt::BlockStmt(SourceLocation loc) : Stmt(std::move(loc)) {}

void BlockStmt::dump(std::ostream &os, int indent) const {
  line(os, indent, "Block");
  for (const auto &item : items) {
    item.dump(os, indent + 1);
  }
}

EmptyStmt::EmptyStmt(SourceLocation loc) : Stmt(std::move(loc)) {}

void EmptyStmt::dump(std::ostream &os, int indent) const {
  line(os, indent, "EmptyStmt");
}

ExprStmt::ExprStmt(SourceLocation loc, ExprPtr expr)
    : Stmt(std::move(loc)), expr(std::move(expr)) {}

void ExprStmt::dump(std::ostream &os, int indent) const {
  line(os, indent, "ExprStmt");
  if (expr) {
    expr->dump(os, indent + 1);
  }
}

AssignStmt::AssignStmt(SourceLocation loc, std::unique_ptr<VarExpr> target,
                       ExprPtr value)
    : Stmt(std::move(loc)), target(std::move(target)), value(std::move(value)) {
}

void AssignStmt::dump(std::ostream &os, int indent) const {
  line(os, indent, "Assign");
  target->dump(os, indent + 1);
  value->dump(os, indent + 1);
}

IfStmt::IfStmt(SourceLocation loc) : Stmt(std::move(loc)) {}

void IfStmt::dump(std::ostream &os, int indent) const {
  line(os, indent, "If");
  line(os, indent + 1, "Cond");
  cond->dump(os, indent + 2);
  line(os, indent + 1, "Then");
  thenBranch->dump(os, indent + 2);
  if (elseBranch) {
    line(os, indent + 1, "Else");
    elseBranch->dump(os, indent + 2);
  }
}

WhileStmt::WhileStmt(SourceLocation loc) : Stmt(std::move(loc)) {}

void WhileStmt::dump(std::ostream &os, int indent) const {
  line(os, indent, "While");
  line(os, indent + 1, "Cond");
  cond->dump(os, indent + 2);
  line(os, indent + 1, "Body");
  body->dump(os, indent + 2);
}

BreakStmt::BreakStmt(SourceLocation loc) : Stmt(std::move(loc)) {}

void BreakStmt::dump(std::ostream &os, int indent) const {
  line(os, indent, "Break");
}

ContinueStmt::ContinueStmt(SourceLocation loc) : Stmt(std::move(loc)) {}

void ContinueStmt::dump(std::ostream &os, int indent) const {
  line(os, indent, "Continue");
}

ReturnStmt::ReturnStmt(SourceLocation loc) : Stmt(std::move(loc)) {}

void ReturnStmt::dump(std::ostream &os, int indent) const {
  line(os, indent, "Return");
  if (value) {
    value->dump(os, indent + 1);
  }
}

VarDef::VarDef(SourceLocation loc, std::string name)
    : Node(std::move(loc)), name(std::move(name)) {}

void VarDef::dump(std::ostream &os, int indent) const {
  line(os, indent, "VarDef " + quote(name));
  for (const auto &dim : dimensions) {
    line(os, indent + 1, "Dim");
    dim->dump(os, indent + 2);
  }
  if (init) {
    init->dump(os, indent + 1);
  }
}

VarDecl::VarDecl(SourceLocation loc, TypeSpec type)
    : Decl(std::move(loc)), type(type) {}

void VarDecl::dump(std::ostream &os, int indent) const {
  line(os, indent, "VarDecl " + type.str());
  for (const auto &def : defs) {
    def->dump(os, indent + 1);
  }
}

FuncParam::FuncParam(SourceLocation loc, TypeSpec type, std::string name)
    : Node(std::move(loc)), type(type), name(std::move(name)) {}

void FuncParam::dump(std::ostream &os, int indent) const {
  line(os, indent,
       "Param " + type.str() + (isArray ? "[]" : "") + " " + quote(name));
  for (const auto &dim : dimensions) {
    line(os, indent + 1, "Dim");
    dim->dump(os, indent + 2);
  }
}

FuncDef::FuncDef(SourceLocation loc, TypeSpec returnType, std::string name)
    : Decl(std::move(loc)), returnType(returnType), name(std::move(name)) {}

void FuncDef::dump(std::ostream &os, int indent) const {
  line(os, indent, "FuncDef " + returnType.str() + " " + quote(name));
  for (const auto &param : params) {
    param->dump(os, indent + 1);
  }
  if (body) {
    body->dump(os, indent + 1);
  }
}

CompUnit::CompUnit(SourceLocation loc) : Node(std::move(loc)) {}

void CompUnit::dump(std::ostream &os, int indent) const {
  line(os, indent, "CompUnit");
  for (const auto &decl : decls) {
    decl->dump(os, indent + 1);
  }
}

} // namespace by

