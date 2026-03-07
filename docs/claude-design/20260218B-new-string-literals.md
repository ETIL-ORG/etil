# New String Literal Words

## GOAL: To create alternative words to the `." `and `s"` words that allow escaping.

- The `." `and `s"` words have the restriction that their string cannot contain a `"` as it 
is the delimiter.
- I propose we add two words `.|` and `s|` that work similar but use the pipe `|` character as the delimiter.
- Furthermore, the new words allow escaping: 
  - `\|` would escape the delimiter
  - `\\` escapes the backslash
  - All the normal C string escaping: `\n`, `\r`, etc.
  - `\%hh` would be special: It is a pattern for hexadecimal character values where `hh` is the byte hex value.