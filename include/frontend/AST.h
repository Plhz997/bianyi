#pragma once

#include "Diagnostics.h"

#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace by {

enum class BuiltinType {
  Int,
  Float,
  Void,
  Unknown,
};

struct TypeSpec {
  BuiltinType base = BuiltinType::Unknown;
  bool isConst = false;

  std::string str() const;
};

struct Node {
  explicit Node(SourceLocation loc) : loc(std::move(loc)) {}
  virtual ~Node() = default;
  virtual void dump(std::ostream &os, int indent) const = 0;

  SourceLocation loc;
};

using NodePtr = std::unique_ptr<Node>;

struct Expr : Node {
  using Node::Node;
};

using ExprPtr = std::unique_ptr<Expr>;

struct IntExpr final : Expr {
  IntExpr(SourceLocation loc, long long value);
  void dump(std::ostream &os, int indent) const override;
  long long value;
};

struct FloatExpr final : Expr {
  FloatExpr(SourceLocation loc, double value);
  void dump(std::ostream &os, int indent) const override;
  double value;
};

struct VarExpr final : Expr {
  VarExpr(SourceLocation loc, std::string name);
  void dump(std::ostream &os, int indent) const override;
  std::string name;
  std::vector<ExprPtr> indices;
};

struct UnaryExpr final : Expr {
  UnaryExpr(SourceLocation loc, std::string op, ExprPtr operand);
  void dump(std::ostream &os, int indent) const override;
  std::string op;
  ExprPtr operand;
};

struct BinaryExpr final : Expr {
  BinaryExpr(SourceLocation loc, std::string op, ExprPtr lhs, ExprPtr rhs);
  void dump(std::ostream &os, int indent) const override;
  std::string op;
  ExprPtr lhs;
  ExprPtr rhs;
};

struct CallExpr final : Expr {
  CallExpr(SourceLocation loc, std::string callee);
  void dump(std::ostream &os, int indent) const override;
  std::string callee;
  std::vector<ExprPtr> args;
};

struct InitVal final : Node {
  explicit InitVal(SourceLocation loc);
  void dump(std::ostream &os, int indent) const override;
  bool isList = false;
  ExprPtr expr;
  std::vector<std::unique_ptr<InitVal>> values;
};

struct Stmt : Node {
  using Node::Node;
};

using StmtPtr = std::unique_ptr<Stmt>;

struct Decl;
using DeclPtr = std::unique_ptr<Decl>;

struct BlockItem {
  DeclPtr decl;
  StmtPtr stmt;
  void dump(std::ostream &os, int indent) const;
};

struct BlockStmt final : Stmt {
  explicit BlockStmt(SourceLocation loc);
  void dump(std::ostream &os, int indent) const override;
  std::vector<BlockItem> items;
};

struct EmptyStmt final : Stmt {
  explicit EmptyStmt(SourceLocation loc);
  void dump(std::ostream &os, int indent) const override;
};

struct ExprStmt final : Stmt {
  ExprStmt(SourceLocation loc, ExprPtr expr);
  void dump(std::ostream &os, int indent) const override;
  ExprPtr expr;
};

struct AssignStmt final : Stmt {
  AssignStmt(SourceLocation loc, std::unique_ptr<VarExpr> target, ExprPtr value);
  void dump(std::ostream &os, int indent) const override;
  std::unique_ptr<VarExpr> target;
  ExprPtr value;
};

struct IfStmt final : Stmt {
  explicit IfStmt(SourceLocation loc);
  void dump(std::ostream &os, int indent) const override;
  ExprPtr cond;
  StmtPtr thenBranch;
  StmtPtr elseBranch;
};

struct WhileStmt final : Stmt {
  explicit WhileStmt(SourceLocation loc);
  void dump(std::ostream &os, int indent) const override;
  ExprPtr cond;
  StmtPtr body;
};

struct BreakStmt final : Stmt {
  explicit BreakStmt(SourceLocation loc);
  void dump(std::ostream &os, int indent) const override;
};

struct ContinueStmt final : Stmt {
  explicit ContinueStmt(SourceLocation loc);
  void dump(std::ostream &os, int indent) const override;
};

struct ReturnStmt final : Stmt {
  explicit ReturnStmt(SourceLocation loc);
  void dump(std::ostream &os, int indent) const override;
  ExprPtr value;
};

struct Decl : Node {
  using Node::Node;
};

struct VarDef final : Node {
  VarDef(SourceLocation loc, std::string name);
  void dump(std::ostream &os, int indent) const override;
  std::string name;
  std::vector<ExprPtr> dimensions;
  std::unique_ptr<InitVal> init;
};

struct VarDecl final : Decl {
  VarDecl(SourceLocation loc, TypeSpec type);
  void dump(std::ostream &os, int indent) const override;
  TypeSpec type;
  std::vector<std::unique_ptr<VarDef>> defs;
};

struct FuncParam final : Node {
  FuncParam(SourceLocation loc, TypeSpec type, std::string name);
  void dump(std::ostream &os, int indent) const override;
  TypeSpec type;
  std::string name;
  bool isArray = false;
  std::vector<ExprPtr> dimensions;
};

struct FuncDef final : Decl {
  FuncDef(SourceLocation loc, TypeSpec returnType, std::string name);
  void dump(std::ostream &os, int indent) const override;
  TypeSpec returnType;
  std::string name;
  std::vector<std::unique_ptr<FuncParam>> params;
  std::unique_ptr<BlockStmt> body;
};

struct CompUnit final : Node {
  explicit CompUnit(SourceLocation loc);
  void dump(std::ostream &os, int indent) const override;
  std::vector<DeclPtr> decls;
};

void dumpIndent(std::ostream &os, int indent);

} // namespace by

