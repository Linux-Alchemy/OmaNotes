# Malformed input must not crash

Unclosed *emphasis and **strong and `code spans

```cpp
an unterminated fence

[broken link](  

[](empty)()[]

Mixed line endings on purpose in this file.

Control characters: [31mnot-a-real-escape[0m

| broken | table
|---
| cell

>>>> quote depth with no closure

Final line proves rendering reached the end.
