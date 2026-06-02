#include "Parser.h"

#include <utility>

namespace by {

Parser::Parser(std::vector<Token> tokens, DiagnosticEngine &diag)
    : tokens_(std::move(tokens)), diag_(diag) {}

std::unique_ptr<CompUnit> Parser::parse() {
  auto unit = std::make_unique<CompUnit>(current().loc);
  while (!isAtEnd()) {
    auto decl = parseTopLevelDecl();
    if (decl) {
      unit->decls.push_back(std::move(decl));
    } else {
      synchronize();
    }
  }
  return unit;
}

const Token &Parser::current() const { return tokens_[pos_]; }

const Token &Parser::previous() const { return tokens_[pos_ - 1]; }

const Token &Parser::peek(int lookahead) const {
  std::size_t idx = pos_ + static_cast<std::size_t>(lookahead);
  if (idx >= tokens_.size()) {
    return tokens_.back();
  }
  return tokens_[idx];
}

bool Parser::isAtEnd() const { return current().kind == TokenKind::EndOfFile; }

bool Parser::check(TokenKind kind) const {
  return !isAtEnd() && current().kind == kind;
}

bool Parser::match(TokenKind kind) {
  if (!check(kind)) {
    return false;
  }
  ++pos_;
  return true;
}

Token Parser::consume(TokenKind kind, const std::string &message) {
  if (check(kind)) {
    return tokens_[pos_++];
  }
  diag_.error(current().loc, message + ", got " + tokenKindName(current().kind));
  return Token{kind, "", current().loc};
}

void Parser::synchronize() {
  while (!isAtEnd()) {
    if (pos_ > 0 && previous().kind == TokenKind::Semicolon) {
      return;
    }
    switch (current().kind) {
    case TokenKind::KwConst:
    case TokenKind::KwInt:
    case TokenKind::KwFloat:
    case TokenKind::KwVoid:
    case TokenKind::KwIf:
    case TokenKind::KwWhile:
    case TokenKind::KwReturn:
    case TokenKind::RBrace:
      return;
    default:
      ++pos_;
      break;
    }
  }
}

TypeSpec Parser::parseType(bool allowVoid) {
  TypeSpec type;
  if (match(TokenKind::KwInt)) {
    type.base = BuiltinType::Int;
  } else if (match(TokenKind::KwFloat)) {
    type.base = BuiltinType::Float;
  } else if (allowVoid && match(TokenKind::KwVoid)) {
    type.base = BuiltinType::Void;
  } else {
    diag_.error(current().loc, "expected type specifier");
    if (!isAtEnd()) {
      ++pos_;
    }
  }
  return type;
}

DeclPtr Parser::parseTopLevelDecl() {
  if (match(TokenKind::KwConst)) {
    return parseVarDecl(true);
  }

  TypeSpec type = parseType(true);
  Token nameTok =
      consume(TokenKind::Identifier, "expected declaration or function name");
  if (match(TokenKind::LParen)) {
    return parseFuncDefAfterName(type, nameTok);
  }

  auto first = parseVarDefAfterName(nameTok);
  return parseVarDeclAfterType(type, std::move(first));
}

std::unique_ptr<VarDecl> Parser::parseVarDecl(bool isConst) {
  TypeSpec type = parseType(false);
  type.isConst = isConst;
  auto decl = std::make_unique<VarDecl>(type.base == BuiltinType::Unknown
                                            ? current().loc
                                            : previous().loc,
                                        type);
  decl->defs.push_back(parseVarDef());
  while (match(TokenKind::Comma)) {
    decl->defs.push_back(parseVarDef());
  }
  consume(TokenKind::Semicolon, "expected ';' after declaration");
  return decl;
}

std::unique_ptr<VarDecl>
Parser::parseVarDeclAfterType(TypeSpec type, std::unique_ptr<VarDef> first) {
  auto decl = std::make_unique<VarDecl>(first ? first->loc : current().loc, type);
  if (first) {
    decl->defs.push_back(std::move(first));
  }
  while (match(TokenKind::Comma)) {
    decl->defs.push_back(parseVarDef());
  }
  consume(TokenKind::Semicolon, "expected ';' after declaration");
  return decl;
}

std::unique_ptr<VarDef> Parser::parseVarDef() {
  Token nameTok = consume(TokenKind::Identifier, "expected variable name");
  return parseVarDefAfterName(nameTok);
}

std::unique_ptr<VarDef> Parser::parseVarDefAfterName(const Token &nameTok) {
  auto def = std::make_unique<VarDef>(nameTok.loc, nameTok.text);
  while (match(TokenKind::LBracket)) {
    def->dimensions.push_back(parseExpression());
    consume(TokenKind::RBracket, "expected ']' after array dimension");
  }
  if (match(TokenKind::Assign)) {
    def->init = parseInitVal();
  }
  return def;
}

std::unique_ptr<InitVal> Parser::parseInitVal() {
  auto init = std::make_unique<InitVal>(current().loc);
  if (match(TokenKind::LBrace)) {
    init->isList = true;
    if (!check(TokenKind::RBrace)) {
      do {
        init->values.push_back(parseInitVal());
      } while (match(TokenKind::Comma) && !check(TokenKind::RBrace));
    }
    consume(TokenKind::RBrace, "expected '}' after initializer list");
  } else {
    init->expr = parseExpression();
  }
  return init;
}

std::unique_ptr<FuncDef>
Parser::parseFuncDefAfterName(TypeSpec returnType, const Token &nameTok) {
  auto func = std::make_unique<FuncDef>(nameTok.loc, returnType, nameTok.text);
  if (!check(TokenKind::RParen)) {
    do {
      func->params.push_back(parseFuncParam());
    } while (match(TokenKind::Comma));
  }
  consume(TokenKind::RParen, "expected ')' after function parameters");
  func->body = parseBlock();
  return func;
}

std::unique_ptr<FuncParam> Parser::parseFuncParam() {
  TypeSpec type = parseType(false);
  Token nameTok = consume(TokenKind::Identifier, "expected parameter name");
  auto param = std::make_unique<FuncParam>(nameTok.loc, type, nameTok.text);
  if (match(TokenKind::LBracket)) {
    param->isArray = true;
    consume(TokenKind::RBracket, "expected ']' after array parameter marker");
    while (match(TokenKind::LBracket)) {
      param->dimensions.push_back(parseExpression());
      consume(TokenKind::RBracket, "expected ']' after parameter dimension");
    }
  }
  return param;
}

std::unique_ptr<BlockStmt> Parser::parseBlock() {
  Token lbrace = consume(TokenKind::LBrace, "expected function or statement body");
  auto block = std::make_unique<BlockStmt>(lbrace.loc);
  while (!isAtEnd() && !check(TokenKind::RBrace)) {
    block->items.push_back(parseBlockItem());
  }
  consume(TokenKind::RBrace, "expected '}' after block");
  return block;
}

BlockItem Parser::parseBlockItem() {
  BlockItem item;
  if (match(TokenKind::KwConst)) {
    item.decl = parseVarDecl(true);
  } else if (check(TokenKind::KwInt) || check(TokenKind::KwFloat)) {
    item.decl = parseVarDecl(false);
  } else {
    item.stmt = parseStmt();
  }
  return item;
}

StmtPtr Parser::parseStmt() {
  if (check(TokenKind::LBrace)) {
    return parseBlock();
  }

  if (match(TokenKind::KwIf)) {
    auto stmt = std::make_unique<IfStmt>(previous().loc);
    consume(TokenKind::LParen, "expected '(' after if");
    stmt->cond = parseExpression();
    consume(TokenKind::RParen, "expected ')' after if condition");
    stmt->thenBranch = parseStmt();
    if (match(TokenKind::KwElse)) {
      stmt->elseBranch = parseStmt();
    }
    return stmt;
  }

  if (match(TokenKind::KwWhile)) {
    auto stmt = std::make_unique<WhileStmt>(previous().loc);
    consume(TokenKind::LParen, "expected '(' after while");
    stmt->cond = parseExpression();
    consume(TokenKind::RParen, "expected ')' after while condition");
    stmt->body = parseStmt();
    return stmt;
  }

  if (match(TokenKind::KwBreak)) {
    auto stmt = std::make_unique<BreakStmt>(previous().loc);
    consume(TokenKind::Semicolon, "expected ';' after break");
    return stmt;
  }

  if (match(TokenKind::KwContinue)) {
    auto stmt = std::make_unique<ContinueStmt>(previous().loc);
    consume(TokenKind::Semicolon, "expected ';' after continue");
    return stmt;
  }

  if (match(TokenKind::KwReturn)) {
    auto stmt = std::make_unique<ReturnStmt>(previous().loc);
    if (!check(TokenKind::Semicolon)) {
      stmt->value = parseExpression();
    }
    consume(TokenKind::Semicolon, "expected ';' after return");
    return stmt;
  }

  if (match(TokenKind::Semicolon)) {
    return std::make_unique<EmptyStmt>(previous().loc);
  }

  if (check(TokenKind::Identifier)) {
    std::size_t saved = pos_;
    auto target = parseLValue();
    if (match(TokenKind::Assign)) {
      SourceLocation loc = target->loc;
      auto value = parseExpression();
      consume(TokenKind::Semicolon, "expected ';' after assignment");
      return std::make_unique<AssignStmt>(loc, std::move(target),
                                          std::move(value));
    }
    pos_ = saved;
  }

  SourceLocation loc = current().loc;
  auto expr = parseExpression();
  consume(TokenKind::Semicolon, "expected ';' after expression");
  return std::make_unique<ExprStmt>(loc, std::move(expr));
}

ExprPtr Parser::parseExpression() { return parseLOr(); }

ExprPtr Parser::parseLOr() {
  auto expr = parseLAnd();
  while (match(TokenKind::LogicalOr)) {
    Token op = previous();
    auto rhs = parseLAnd();
    expr = std::make_unique<BinaryExpr>(op.loc, op.text, std::move(expr),
                                        std::move(rhs));
  }
  return expr;
}

ExprPtr Parser::parseLAnd() {
  auto expr = parseEquality();
  while (match(TokenKind::LogicalAnd)) {
    Token op = previous();
    auto rhs = parseEquality();
    expr = std::make_unique<BinaryExpr>(op.loc, op.text, std::move(expr),
                                        std::move(rhs));
  }
  return expr;
}

ExprPtr Parser::parseEquality() {
  auto expr = parseRelational();
  while (match(TokenKind::Equal) || match(TokenKind::NotEqual)) {
    Token op = previous();
    auto rhs = parseRelational();
    expr = std::make_unique<BinaryExpr>(op.loc, op.text, std::move(expr),
                                        std::move(rhs));
  }
  return expr;
}

ExprPtr Parser::parseRelational() {
  auto expr = parseAdditive();
  while (match(TokenKind::Less) || match(TokenKind::LessEqual) ||
         match(TokenKind::Greater) || match(TokenKind::GreaterEqual)) {
    Token op = previous();
    auto rhs = parseAdditive();
    expr = std::make_unique<BinaryExpr>(op.loc, op.text, std::move(expr),
                                        std::move(rhs));
  }
  return expr;
}

ExprPtr Parser::parseAdditive() {
  auto expr = parseMultiplicative();
  while (match(TokenKind::Plus) || match(TokenKind::Minus)) {
    Token op = previous();
    auto rhs = parseMultiplicative();
    expr = std::make_unique<BinaryExpr>(op.loc, op.text, std::move(expr),
                                        std::move(rhs));
  }
  return expr;
}

ExprPtr Parser::parseMultiplicative() {
  auto expr = parseUnary();
  while (match(TokenKind::Star) || match(TokenKind::Slash) ||
         match(TokenKind::Percent)) {
    Token op = previous();
    auto rhs = parseUnary();
    expr = std::make_unique<BinaryExpr>(op.loc, op.text, std::move(expr),
                                        std::move(rhs));
  }
  return expr;
}

ExprPtr Parser::parseUnary() {
  if (match(TokenKind::Plus) || match(TokenKind::Minus) ||
      match(TokenKind::Bang)) {
    Token op = previous();
    return std::make_unique<UnaryExpr>(op.loc, op.text, parseUnary());
  }

  if (check(TokenKind::Identifier) && peek(1).kind == TokenKind::LParen) {
    Token name = consume(TokenKind::Identifier, "expected function name");
    consume(TokenKind::LParen, "expected '(' after function name");
    auto call = std::make_unique<CallExpr>(name.loc, name.text);
    if (!check(TokenKind::RParen)) {
      do {
        call->args.push_back(parseExpression());
      } while (match(TokenKind::Comma));
    }
    consume(TokenKind::RParen, "expected ')' after function call");
    return call;
  }

  return parsePrimary();
}

ExprPtr Parser::parsePrimary() {
  if (match(TokenKind::LParen)) {
    auto expr = parseExpression();
    consume(TokenKind::RParen, "expected ')' after expression");
    return expr;
  }

  if (match(TokenKind::IntLiteral)) {
    return std::make_unique<IntExpr>(previous().loc, previous().intValue);
  }

  if (match(TokenKind::FloatLiteral)) {
    return std::make_unique<FloatExpr>(previous().loc, previous().floatValue);
  }

  if (check(TokenKind::Identifier)) {
    return parseLValue();
  }

  diag_.error(current().loc, "expected expression");
  Token bad = current();
  if (!isAtEnd()) {
    ++pos_;
  }
  return std::make_unique<IntExpr>(bad.loc, 0);
}

std::unique_ptr<VarExpr> Parser::parseLValue() {
  Token name = consume(TokenKind::Identifier, "expected identifier");
  auto expr = std::make_unique<VarExpr>(name.loc, name.text);
  while (match(TokenKind::LBracket)) {
    expr->indices.push_back(parseExpression());
    consume(TokenKind::RBracket, "expected ']' after subscript");
  }
  return expr;
}

} // namespace by

