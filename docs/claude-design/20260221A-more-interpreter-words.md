# TASK: Add more words.

- GOAL: Flesh out the interpreter with more words.
- Commit often to provide a git blame trail if problems ensue.

# FORTH Words:

## Implement the following words: Refer to the FORTH spec for tradidional definitions.

- 1+
- 1-
- abort
- depth
- fill 
  - Name it sfill to conform with the string word naming pattern.
- 
- find
- invert
- j
- leave
- literal
- lshift
- move
  - Name it bytes-move  
  - Only works on byte arrays.
  - Add PEN tests for this word too.


# Non-FORTH Words:
- array-slice
- array-splice
- sprintf
  - Actually works more like C++ vsprintf
  - The number of format items determining how many items are popped from the stack.
  - Check stack before performing format: Insufficient items halts execution and sends error to ctx.err() 