# Editor support

Syntax highlighting for `.fire` files.

## Vim / Neovim

```console
$ mkdir -p ~/.vim/syntax ~/.vim/ftdetect
$ cp editors/vim/syntax/fire.vim    ~/.vim/syntax/
$ cp editors/vim/ftdetect/fire.vim  ~/.vim/ftdetect/
```

Neovim reads the same layout under `~/.config/nvim/`. With a plugin manager,
point it at the `editors/vim` directory instead.

## VS Code

```console
$ cp -r editors/vscode ~/.vscode/extensions/fire-lang-1.0.0
```

Reload the window. `.fire` files are then recognised, with comment toggling,
bracket matching and auto-indent as well as highlighting.

To package it properly:

```console
$ cd editors/vscode && npx vsce package
```

## Anything else

The TextMate grammar in `editors/vscode/syntaxes/fire.tmLanguage.json` works
unmodified in Sublime Text, Zed, and anything else that reads TextMate
grammars. The scope name is `source.fire`.

## What is highlighted

Both grammars follow the real lexer rather than approximating it: block
comments nest, escape sequences are validated (an unknown `\q` is marked
invalid), all four integer bases are recognised with `_` separators, and the
builtins are highlighted only in call position — they are not reserved words in
Fire, so `let len = 3;` is legal and should not look like a keyword.
