#include "../../include/frontend/Lexer.h"

#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <unordered_map>

namespace by {

Lexer::Lexer(std::string fileName, std::string source, DiagnosticEngine &diag)
    : fileName_(std::move(fileName)), source_(std::move(source)), diag_(diag) {}

std::vector<Token> Lexer::tokenize() {
  std::vector<Token> tokens;
  while (true) {
    skipWhitespaceAndComments();
    if (isAtEnd()) {
      Token eof;
      eof.kind = TokenKind::EndOfFile;
      eof.loc = currentLoc();
      tokens.push_back(eof);
      break;
    }

    char c = peek();
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
      tokens.push_back(lexIdentifier());
    } else if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
      tokens.push_back(lexNumber());
    } else {
      tokens.push_back(lexOperatorOrPunct());
    }
  }
  return tokens;
}

bool Lexer::isAtEnd() const { return pos_ >= source_.size(); }

char Lexer::peek(int lookahead) const {
  std::size_t idx = pos_ + static_cast<std::size_t>(lookahead);
  if (idx >= source_.size()) {
    return '\0';
  }
  return source_[idx];
}

char Lexer::advance() {
  char c = peek();
  if (isAtEnd()) {
    return '\0';
  }
  ++pos_;
  if (c == '\n') {
    ++line_;
    column_ = 1;
  } else {
    ++column_;
  }
  return c;
}

bool Lexer::match(char expected) {
  if (peek() != expected) {
    return false;
  }
  advance();
  return true;
}

SourceLocation Lexer::currentLoc() const {
  return SourceLocation{fileName_, pos_, line_, column_};
}

void Lexer::skipWhitespaceAndComments() {
  while (!isAtEnd()) {
    char c = peek();
    if (std::isspace(static_cast<unsigned char>(c))) {
      advance();
      continue;
    }

    if (c == '/' && peek(1) == '/') {
      while (!isAtEnd() && peek() != '\n') {
        advance();
      }
      continue;
    }

    if (c == '/' && peek(1) == '*') {
      SourceLocation loc = currentLoc();
      advance();
      advance();
      bool closed = false;
      while (!isAtEnd()) {
        if (peek() == '*' && peek(1) == '/') {
          advance();
          advance();
          closed = true;
          break;
        }
        advance();
      }
      if (!closed) {
        diag_.error(loc, "unterminated block comment");
      }
      continue;
    }

    break;
  }
}

Token Lexer::makeToken(TokenKind kind, SourceLocation loc,
                       std::size_t start) const {
  Token tok;
  tok.kind = kind;
  tok.loc = std::move(loc);
  tok.text = source_.substr(start, pos_ - start);
  return tok;
}

Token Lexer::lexIdentifier() {
  SourceLocation loc = currentLoc();
  std::size_t start = pos_;
  while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') {
    advance();
  }

  Token tok = makeToken(TokenKind::Identifier, loc, start);
  static const std::unordered_map<std::string, TokenKind> keywords = {
      {"const", TokenKind::KwConst},       {"int", TokenKind::KwInt},
      {"float", TokenKind::KwFloat},       {"void", TokenKind::KwVoid},
      {"if", TokenKind::KwIf},             {"else", TokenKind::KwElse},
      {"while", TokenKind::KwWhile},       {"break", TokenKind::KwBreak},
      {"continue", TokenKind::KwContinue}, {"return", TokenKind::KwReturn},
  };
  auto it = keywords.find(tok.text);
  if (it != keywords.end()) {
    tok.kind = it->second;
  }
  return tok;
}

Token Lexer::lexNumber() {
  SourceLocation loc = currentLoc();
  std::size_t start = pos_;
  bool isFloat = false;

  if (peek() == '.') {
    isFloat = true;
    advance();
    if (!std::isdigit(static_cast<unsigned char>(peek()))) {
      Token tok = makeToken(TokenKind::Invalid, loc, start);
      diag_.error(loc, "unexpected '.'");
      return tok;
    }
    while (std::isdigit(static_cast<unsigned char>(peek()))) {
      advance();
    }
    if (peek() == 'e' || peek() == 'E') {
      advance();
      if (peek() == '+' || peek() == '-') {
        advance();
      }
      while (std::isdigit(static_cast<unsigned char>(peek()))) {
        advance();
      }
    }
  } else if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
    advance();
    advance();
    while (std::isxdigit(static_cast<unsigned char>(peek()))) {
      advance();
    }
    if (peek() == '.' || peek() == 'p' || peek() == 'P') {
      isFloat = true;
      if (peek() == '.') {
        advance();
        while (std::isxdigit(static_cast<unsigned char>(peek()))) {
          advance();
        }
      }
      if (peek() == 'p' || peek() == 'P') {
        advance();
        if (peek() == '+' || peek() == '-') {
          advance();
        }
        while (std::isdigit(static_cast<unsigned char>(peek()))) {
          advance();
        }
      }
    }
  } else {
    while (std::isdigit(static_cast<unsigned char>(peek()))) {
      advance();
    }
    if (peek() == '.') {
      isFloat = true;
      advance();
      while (std::isdigit(static_cast<unsigned char>(peek()))) {
        advance();
      }
    }
    if (peek() == 'e' || peek() == 'E') {
      isFloat = true;
      advance();
      if (peek() == '+' || peek() == '-') {
        advance();
      }
      while (std::isdigit(static_cast<unsigned char>(peek()))) {
        advance();
      }
    }
  }

  Token tok = makeToken(isFloat ? TokenKind::FloatLiteral
                                : TokenKind::IntLiteral,
                        loc, start);
  errno = 0;
  if (isFloat) {
    tok.floatValue = std::strtod(tok.text.c_str(), nullptr);
  } else {
    tok.intValue = std::strtoll(tok.text.c_str(), nullptr, 0);
  }
  if (errno == ERANGE) {
    diag_.error(loc, "numeric literal is out of range");
  }
  return tok;
}

Token Lexer::lexOperatorOrPunct() {
  SourceLocation loc = currentLoc();
  std::size_t start = pos_;
  char c = advance();
  switch (c) {
  case '+':
    return makeToken(TokenKind::Plus, loc, start);
  case '-':
    return makeToken(TokenKind::Minus, loc, start);
  case '*':
    return makeToken(TokenKind::Star, loc, start);
  case '/':
    return makeToken(TokenKind::Slash, loc, start);
  case '%':
    return makeToken(TokenKind::Percent, loc, start);
  case '!':
    if (match('=')) {
      return makeToken(TokenKind::NotEqual, loc, start);
    }
    return makeToken(TokenKind::Bang, loc, start);
  case '=':
    if (match('=')) {
      return makeToken(TokenKind::Equal, loc, start);
    }
    return makeToken(TokenKind::Assign, loc, start);
  case '<':
    if (match('=')) {
      return makeToken(TokenKind::LessEqual, loc, start);
    }
    return makeToken(TokenKind::Less, loc, start);
  case '>':
    if (match('=')) {
      return makeToken(TokenKind::GreaterEqual, loc, start);
    }
    return makeToken(TokenKind::Greater, loc, start);
  case '&':
    if (match('&')) {
      return makeToken(TokenKind::LogicalAnd, loc, start);
    }
    break;
  case '|':
    if (match('|')) {
      return makeToken(TokenKind::LogicalOr, loc, start);
    }
    break;
  case ',':
    return makeToken(TokenKind::Comma, loc, start);
  case ';':
    return makeToken(TokenKind::Semicolon, loc, start);
  case '(':
    return makeToken(TokenKind::LParen, loc, start);
  case ')':
    return makeToken(TokenKind::RParen, loc, start);
  case '[':
    return makeToken(TokenKind::LBracket, loc, start);
  case ']':
    return makeToken(TokenKind::RBracket, loc, start);
  case '{':
    return makeToken(TokenKind::LBrace, loc, start);
  case '}':
    return makeToken(TokenKind::RBrace, loc, start);
  default:
    break;
  }

  Token tok = makeToken(TokenKind::Invalid, loc, start);
  diag_.error(loc, "unexpected character '" + tok.text + "'");
  return tok;
}

} // namespace by
