# Intermediate Representation Design

The middle layer lowers checked AST into a simple three-address IR. It is meant
to be readable first, then gradually optimized and lowered to RISC-V.

## Shape

```text
Module
  Global
  Function
    BasicBlock
      Instruction
```

Values are represented as named temporaries such as `%t0`, local storage slots
such as `%x.addr.0`, global addresses such as `@a`, and constants.

## Core Instructions

- `alloca type`
- `load addr`
- `store value, addr`
- `gep base, index...`
- `add/sub/mul/div/rem lhs, rhs`
- `eq/ne/lt/le/gt/ge lhs, rhs`
- `neg value`
- `call @name(args...)`
- `br label`
- `br cond, true_label, false_label`
- `ret value`

Array initialization is currently kept as a high-level `init` instruction. The
backend-lowering pass will later expand it into stores or data-section records.

## Control Flow

`if`, `while`, `break`, `continue`, and `return` are lowered to explicit basic
blocks and branches. Logical `&&` and `||` are lowered through `genCond`, so
short-circuit evaluation is preserved and expressions with function calls keep
the correct side effects.
