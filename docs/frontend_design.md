# Frontend Design

The frontend converts SysY2026 source code into a checked AST. It is split into
five modules:

- `Lexer`: scans source text into tokens and records source locations.
- `Parser`: builds the AST with recursive descent and expression precedence.
- `AST`: owns declarations, statements, expressions, and AST dumping.
- `Sema`: performs symbol-table and type-oriented checks.
- `Diagnostics`: reports lexical, syntax, and semantic errors.

The parser keeps short-circuit expressions as `&&` and `||` nodes. Code
generation will lower those nodes to control flow so calls with side effects are
not evaluated too early.

The symbol table is scope based:

```text
global
  function
    block
      nested block
```

Runtime library functions such as `getint`, `putint`, `getarray`, `starttime`,
and `stoptime` are predeclared by semantic analysis.

