#pragma once

#include "AST.h"
#include "IR.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace by::ir {

class IRGenerator {
public:
  Module generate(const CompUnit &unit);

private:
  struct Binding {
    std::string address;
    std::string type;
    bool isArray = false;
  };

  struct LoopTarget {
    std::string continueLabel;
    std::string breakLabel;
  };

  void collectFunctionTypes(const CompUnit &unit);
  void genTopLevelDecl(const Decl &decl);
  void genGlobalDecl(const VarDecl &decl);
  void genFunction(const FuncDef &func);
  void genBlock(const BlockStmt &block, bool createsScope = true);
  void genBlockItem(const BlockItem &item);
  void genVarDecl(const VarDecl &decl, bool global);
  void genStmt(const Stmt &stmt);
  std::string genExpr(const Expr &expr);
  std::string genLValue(const VarExpr &expr);
  void genCond(const Expr &expr, const std::string &trueLabel,
               const std::string &falseLabel);

  std::string emitBinary(const BinaryExpr &expr);
  std::string emitShortCircuitValue(const BinaryExpr &expr);
  std::string renderInit(const InitVal *init) const;
  std::string renderType(const TypeSpec &type, bool isArray = false,
                         const std::vector<ExprPtr> *dims = nullptr) const;
  std::string renderExprPreview(const Expr &expr) const;
  std::string functionReturnType(const std::string &name) const;

  void pushScope();
  void popScope();
  void bind(const std::string &name, Binding binding);
  const Binding *lookup(const std::string &name) const;

  std::string freshTemp();
  std::string freshLabel(const std::string &base);
  std::string freshLocalName(const std::string &base);
  std::string valueName(const std::string &base) const;

  void startBlock(const std::string &label);
  void emit(const std::string &text);
  void terminate(const std::string &text);
  bool currentBlockTerminated() const;

  Module module_;
  Function *currentFunction_ = nullptr;
  BasicBlock *currentBlock_ = nullptr;
  std::vector<std::unordered_map<std::string, Binding>> scopes_;
  std::unordered_map<std::string, Binding> globals_;
  std::unordered_map<std::string, std::string> functionReturns_;
  std::vector<LoopTarget> loops_;
  int tempId_ = 0;
  int labelId_ = 0;
  int localId_ = 0;
};

} // namespace by::ir
