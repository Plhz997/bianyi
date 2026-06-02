#pragma once

#include "AST.h"
#include "Diagnostics.h"
#include "Token.h"

#include <memory>
#include <vector>

namespace by {

class Parser {
public:
  Parser(std::vector<Token> tokens, DiagnosticEngine &diag);

  std::unique_ptr<CompUnit> parse();

private:
  const Token &current() const;
  const Token &previous() const;
  const Token &peek(int lookahead) const;
  bool isAtEnd() const;
  bool check(TokenKind kind) const;
  bool match(TokenKind kind);
  Token consume(TokenKind kind, const std::string &message);
  void synchronize();

  TypeSpec parseType(bool allowVoid);
  DeclPtr parseTopLevelDecl();
  std::unique_ptr<VarDecl> parseVarDecl(bool isConst);
  std::unique_ptr<VarDecl> parseVarDeclAfterType(TypeSpec type,
                                                 std::unique_ptr<VarDef> first);
  std::unique_ptr<VarDef> parseVarDef();
  std::unique_ptr<VarDef> parseVarDefAfterName(const Token &nameTok);
  std::unique_ptr<InitVal> parseInitVal();
  std::unique_ptr<FuncDef> parseFuncDefAfterName(TypeSpec returnType,
                                                 const Token &nameTok);
  std::unique_ptr<FuncParam> parseFuncParam();

  std::unique_ptr<BlockStmt> parseBlock();
  BlockItem parseBlockItem();
  StmtPtr parseStmt();

  ExprPtr parseExpression();
  ExprPtr parseLOr();
  ExprPtr parseLAnd();
  ExprPtr parseEquality();
  ExprPtr parseRelational();
  ExprPtr parseAdditive();
  ExprPtr parseMultiplicative();
  ExprPtr parseUnary();
  ExprPtr parsePrimary();
  std::unique_ptr<VarExpr> parseLValue();

  std::vector<Token> tokens_;
  DiagnosticEngine &diag_;
  std::size_t pos_ = 0;
};

} // namespace by

