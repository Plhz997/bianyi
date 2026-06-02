# bianyi

SysY2026-to-RISC-V compiler project for the compiler-system design contest.

This version provides a small but complete compiler pipeline:

- character-stream lexer with source locations
- recursive-descent parser
- AST model and AST dumping
- scope-based symbol table
- basic semantic checks
- competition-style command-line driver
- textual IR generation for debugging
- direct RISC-V 64GC assembly generation for the integer SysY subset

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

The backend currently emits self-contained RV64GC assembly for integer scalar
values, integer arrays, functions, calls, if/while control flow, break/continue,
returns, relational/logical expressions, and the standard SysY runtime calls.
Float/vector lowering and aggressive optimization are planned follow-up work.
