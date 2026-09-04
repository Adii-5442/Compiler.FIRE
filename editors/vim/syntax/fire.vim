" Vim syntax file for the Fire programming language.
" Language: Fire
" URL:      https://github.com/Adii-5442/Compiler.FIRE

if exists("b:current_syntax")
  finish
endif

" Keywords
syn keyword fireKeyword     let const fn return
syn keyword fireConditional if elif else
syn keyword fireRepeat      while for in
syn keyword fireStatement   break continue
syn keyword fireBoolean     true false
syn keyword fireType        int float bool str void

" Builtin functions. Not reserved words in Fire, but highlighting them the
" way a language server would is more useful than pretending they are ordinary
" identifiers.
syn keyword fireBuiltin     print println input eof
syn keyword fireBuiltin     len push pop insert remove slice
syn keyword fireBuiltin     find split join trim upper lower replace repeat chr ord
syn keyword fireBuiltin     abs min max pow sqrt floor ceil round seed rand_int
syn keyword fireBuiltin     exit panic assert clock args

" Literals
syn match   fireNumber      "\<\d[0-9_]*\>"
syn match   fireNumber      "\<0[xX][0-9a-fA-F_]\+\>"
syn match   fireNumber      "\<0[bB][01_]\+\>"
syn match   fireNumber      "\<0[oO][0-7_]\+\>"
syn match   fireFloat       "\<\d[0-9_]*\.\d[0-9_]*\([eE][-+]\=\d\+\)\=\>"
syn match   fireFloat       "\<\d[0-9_]*[eE][-+]\=\d\+\>"

syn match   fireEscape      contained "\\\%([nrte0\\\"]\|x\x\{2}\|u{\x\{1,6}}\)"
syn region  fireString      start=+"+ skip=+\\\\\|\\"+ end=+"+ contains=fireEscape

" Comments. Fire's block comments nest, which `contains=fireBlockComment`
" expresses directly.
syn match   fireLineComment "//.*$" contains=fireTodo
syn region  fireBlockComment start="/\*" end="\*/" contains=fireBlockComment,fireTodo
syn keyword fireTodo        contained TODO FIXME NOTE XXX

" Operators and delimiters
syn match   fireOperator    "[-+*/%!~^&|<>=]"
syn match   fireOperator    "&&\|||\|==\|!=\|<=\|>=\|<<\|>>\|+=\|-=\|\*=\|/=\|%=\|->\|\.\."
syn match   fireDelimiter   "[(){}\[\],;:]"

" A function name at its declaration, and at a call site.
syn match   fireFunction    "\<fn\s\+\zs\w\+"
syn match   fireCall        "\<\w\+\ze\s*(" contains=fireBuiltin,fireType

hi def link fireKeyword       Keyword
hi def link fireConditional   Conditional
hi def link fireRepeat        Repeat
hi def link fireStatement     Statement
hi def link fireBoolean       Boolean
hi def link fireType          Type
hi def link fireBuiltin       Function
hi def link fireNumber        Number
hi def link fireFloat         Float
hi def link fireString        String
hi def link fireEscape        SpecialChar
hi def link fireLineComment   Comment
hi def link fireBlockComment  Comment
hi def link fireTodo          Todo
hi def link fireOperator      Operator
hi def link fireDelimiter     Delimiter
hi def link fireFunction      Function
hi def link fireCall          Identifier

let b:current_syntax = "fire"
