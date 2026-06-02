#pragma once

#include "Diagnostics.h"
#include "Token.h"

#include <string>
#include <vector>

namespace by {

class Lexer {
public:
  Lexer(std::string fileName, std::string source, DiagnosticEngine &diag);

  std::vector<Token> tokenize();

private:
  bool isAtEnd() const;
  char peek(int lookahead = 0) const;
  char advance();
  bool match(char expected);
  SourceLocation currentLoc() const;

  void skipWhitespaceAndComments();
  Token makeToken(TokenKind kind, SourceLocation loc, std::size_t start) const;
  Token lexIdentifier();
  Token lexNumber();
  Token lexOperatorOrPunct();

  std::string fileName_;
  std::string source_;
  DiagnosticEngine &diag_;
  std::size_t pos_ = 0;
  int line_ = 1;
  int column_ = 1;
};

} // namespace by

