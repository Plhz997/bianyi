#pragma once

#include "AST.h"
#include "Diagnostics.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace by {

struct ValueType {
  BuiltinType base = BuiltinType::Unknown;
  bool isArray = false;
  bool isConst = false;

  std::string str() const;
};

struct Symbol {
  enum class Kind {
    Variable,
    Function,
  };

  Kind kind = Kind::Variable;
  ValueType type;
  std::vector<ValueType> params;
  std::vector<long long> dimensions;
  std::optional<long long> constInt;
};

class SemanticAnalyzer {
public:
  explicit SemanticAnalyzer(DiagnosticEngine &diag);

  bool analyze(CompUnit &unit);

private:
  using Scope = std::unordered_map<std::string, Symbol>;

  void pushScope();
  void popScope();
  Symbol *lookup(const std::string &name);
  Symbol *lookupCurrent(const std::string &name);
  bool declare(const std::string &name, const Symbol &symbol,
               const SourceLocation &loc);

  void installBuiltins();
  void predeclareFunction(FuncDef &func);
  void checkTopLevelDecl(Decl &decl);
  void checkVarDecl(VarDecl &decl, bool global);
  void checkFuncDef(FuncDef &func);
  void checkBlock(BlockStmt &block, bool createsScope = true);
  void checkStmt(Stmt &stmt);
  ValueType checkExpr(Expr &expr);
  ValueType checkInitVal(InitVal &init);

  std::optional<long long> evalConstInt(Expr &expr);
  ValueType scalar(BuiltinType base, bool isConst = false) const;
  ValueType arrayOf(BuiltinType base, bool isConst = false) const;
  bool isNumeric(ValueType type) const;
  bool isAssignable(ValueType target, ValueType value) const;

  DiagnosticEngine &diag_;
  std::vector<Scope> scopes_;
  BuiltinType currentReturn_ = BuiltinType::Unknown;
  int loopDepth_ = 0;
};

} // namespace by

