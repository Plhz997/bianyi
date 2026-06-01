#pragma once

#include "frontend/Diagnostics.h"

#include <cstdint>
#include <string>

namespace by {

enum class TokenKind {
  EndOfFile,
  Invalid,

  Identifier,
  IntLiteral,
  FloatLiteral,

  KwConst,
  KwInt,
  KwFloat,
  KwVoid,
  KwIf,
  KwElse,
  KwWhile,
  KwBreak,
  KwContinue,
  KwReturn,

  Plus,
  Minus,
  Star,
  Slash,
  Percent,
  Bang,
  Assign,
  Equal,
  NotEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  LogicalAnd,
  LogicalOr,

  Comma,
  Semicolon,
  LParen,
  RParen,
  LBracket,
  RBracket,
  LBrace,
  RBrace,
};

struct Token {
  TokenKind kind = TokenKind::Invalid;
  std::string text;
  SourceLocation loc;
  std::int64_t intValue = 0;
  double floatValue = 0.0;
};

const char *tokenKindName(TokenKind kind);
bool isTypeKeyword(TokenKind kind);

} // namespace by

