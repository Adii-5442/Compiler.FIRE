# Fire grammar

The complete syntax of Fire, in EBNF. `{ x }` means zero or more, `[ x ]`
means optional, `|` separates alternatives. Terminals are quoted.

The parser in [`src/parser.cpp`](../src/parser.cpp) follows this file
production by production, so a change here should be a change there.

## Lexical structure

```ebnf
letter        = "A".."Z" | "a".."z" | "_" ;
digit         = "0".."9" ;
hex_digit     = digit | "a".."f" | "A".."F" ;

identifier    = letter , { letter | digit } ;

int_literal   = decimal | hex | binary | octal ;
decimal       = digit , { digit | "_" } ;
hex           = "0" , ( "x" | "X" ) , hex_digit , { hex_digit | "_" } ;
binary        = "0" , ( "b" | "B" ) , ( "0" | "1" ) , { "0" | "1" | "_" } ;
octal         = "0" , ( "o" | "O" ) , "0".."7" , { "0".."7" | "_" } ;

float_literal = decimal , [ "." , decimal ] , [ exponent ]
              (* at least one of the fraction or the exponent must be present *)
exponent      = ( "e" | "E" ) , [ "+" | "-" ] , digit , { digit } ;

string        = '"' , { character | escape } , '"' ;
escape        = "\\" , ( "n" | "t" | "r" | "0" | "e" | "\\" | '"'
                       | "x" , hex_digit , hex_digit
                       | "u" , "{" , hex_digit * 1..6 , "}" ) ;

comment       = "//" , { any_character_except_newline }
              | "/*" , { comment | any_character } , "*/" ;   (* nests *)
```

A `.` begins a fractional part only when a digit follows it. That is what lets
`0..10` lex as `0`, `..`, `10` rather than as a malformed float.

Comments and whitespace are trivia: they may appear between any two tokens and
are discarded by the lexer.

## Program structure

```ebnf
program       = { item } ;
item          = function | statement ;

function      = "fn" , identifier , "(" , [ parameters ] , ")" ,
                [ "->" , type ] , block ;
parameters    = parameter , { "," , parameter } , [ "," ] ;
parameter     = identifier , ":" , type ;

type          = "int" | "float" | "bool" | "str" | "void"
              | "[" , type , "]" ;
```

Functions may be declared only at the top level, and are visible throughout the
file regardless of order. A missing `-> type` means the function returns
nothing.

## Statements

```ebnf
statement     = var_decl
              | if_statement
              | while_statement
              | for_statement
              | return_statement
              | "break" , ";"
              | "continue" , ";"
              | block
              | simple_statement
              | ";" ;                        (* an empty statement *)

var_decl      = ( "let" | "const" ) , identifier , [ ":" , type ] ,
                "=" , expression , ";" ;

block         = "{" , { statement } , "}" ;

if_statement  = "if" , expression , block ,
                { "elif" , expression , block } ,
                [ "else" , block ] ;

while_statement = "while" , expression , block ;

for_statement = "for" , identifier , "in" , expression ,
                [ ".." , expression ] , block ;

return_statement = "return" , [ expression ] , ";" ;

simple_statement = expression , [ assign_op , expression ] , ";" ;
assign_op     = "=" | "+=" | "-=" | "*=" | "/=" | "%=" ;
```

A condition is an ordinary expression with no surrounding parentheses; the
block's `{` is what ends it. The left side of an assignment must be a name or
an index expression.

With a `..` the `for` iterates a half-open integer range; without one it
iterates an array or a string.

## Expressions

Levels are listed loosest to tightest. Every binary level is left associative.

```ebnf
expression    = logical_or ;
logical_or    = logical_and , { "||" , logical_and } ;
logical_and   = equality , { "&&" , equality } ;
equality      = comparison , { ( "==" | "!=" ) , comparison } ;
comparison    = bit_or , { ( "<" | "<=" | ">" | ">=" ) , bit_or } ;
bit_or        = bit_xor , { "|" , bit_xor } ;
bit_xor       = bit_and , { "^" , bit_and } ;
bit_and       = shift , { "&" , shift } ;
shift         = term , { ( "<<" | ">>" ) , term } ;
term          = factor , { ( "+" | "-" ) , factor } ;
factor        = unary , { ( "*" | "/" | "%" ) , unary } ;
unary         = ( "-" | "!" | "~" ) , unary | postfix ;
postfix       = primary , { "[" , expression , "]" } ;

primary       = int_literal
              | float_literal
              | string
              | "true" | "false"
              | identifier
              | call
              | "(" , expression , ")"
              | "[" , [ expression , { "," , expression } , [ "," ] ] , "]" ;

call          = ( identifier | "int" | "float" | "bool" | "str" ) ,
                "(" , [ expression , { "," , expression } , [ "," ] ] , ")" ;
```

`int`, `float`, `bool` and `str` are type keywords, but a type keyword directly
followed by `(` is a call to the conversion builtin of the same name.

Fire has no first-class functions, so a callee is always a name known at
compile time. There is no member access operator, no ternary conditional and no
assignment expression — assignment is a statement.

## Reserved words

```
let    const  fn     return  if     elif   else
while  for    in     break   continue
true   false  int    float   bool   str    void
```

Builtin function names (`print`, `len`, `push`, …) are *not* reserved: they live
in a separate namespace from variables, so `let len = 3;` is legal, though the
compiler will then stop you calling `len(...)` as a function in that scope.
