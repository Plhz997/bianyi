#include "../../include/frontend/Token.h"

namespace by {

const char *tokenKindName(TokenKind kind) {
  switch (kind) {
  case TokenKind::EndOfFile:
    return "end of file";
  case TokenKind::Invalid:
    return "invalid token";
  case TokenKind::Identifier:
    return "identifier";
  case TokenKind::IntLiteral:
    return "integer literal";
  case TokenKind::FloatLiteral:
    return "float literal";
  case TokenKind::KwConst:
    return "const";
  case TokenKind::KwInt:
    return "int";
  case TokenKind::KwFloat:
    return "float";
  case TokenKind::KwVoid:
    return "void";
  case TokenKind::KwIf:
    return "if";
  case TokenKind::KwElse:
    return "else";
  case TokenKind::KwWhile:
    return "while";
  case TokenKind::KwBreak:
    return "break";
  case TokenKind::KwContinue:
    return "continue";
  case TokenKind::KwReturn:
    return "return";
  case TokenKind::Plus:
    return "+";
  case TokenKind::Minus:
    return "-";
  case TokenKind::Star:
    return "*";
  case TokenKind::Slash:
    return "/";
  case TokenKind::Percent:
    return "%";
  case TokenKind::Bang:
    return "!";
  case TokenKind::Assign:
    return "=";
  case TokenKind::Equal:
    return "==";
  case TokenKind::NotEqual:
    return "!=";
  case TokenKind::Less:
    return "<";
  case TokenKind::LessEqual:
    return "<=";
  case TokenKind::Greater:
    return ">";
  case TokenKind::GreaterEqual:
    return ">=";
  case TokenKind::LogicalAnd:
    return "&&";
  case TokenKind::LogicalOr:
    return "||";
  case TokenKind::Comma:
    return ",";
  case TokenKind::Semicolon:
    return ";";
  case TokenKind::LParen:
    return "(";
  case TokenKind::RParen:
    return ")";
  case TokenKind::LBracket:
    return "[";
  case TokenKind::RBracket:
    return "]";
  case TokenKind::LBrace:
    return "{";
  case TokenKind::RBrace:
    return "}";
  }
  return "unknown token";
}

bool isTypeKeyword(TokenKind kind) {
  return kind == TokenKind::KwInt || kind == TokenKind::KwFloat ||
         kind == TokenKind::KwVoid;
}

} // namespace by
