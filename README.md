# bianyi

SysY2026-to-RISC-V compiler project for the compiler-system design contest.

This initial version focuses on the compiler frontend:

- character-stream lexer with source locations
- recursive-descent parser
- AST model and AST dumping
- scope-based symbol table
- basic semantic checks
- competition-style command-line driver

## Build

```bash
make
```

The build produces an executable named `compiler`.

## Usage

Frontend debugging:

```bash
./compiler tests/basic.sy --dump-ast
./compiler tests/basic.sy --dump-ir
```

Competition-style command:

```bash
./compiler testcase.sysy -S -o testcase.s
./compiler testcase.sysy -S -o testcase.s -O1
```

The backend is intentionally minimal for now: it emits a small placeholder
assembly file after parsing, semantic checks, and IR generation succeed.
Full RISC-V code generation will be added in a later milestone.
