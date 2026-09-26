NEPTUNE UNIVERSAL INSTALLER

This package installs the Neptune interpreter and syntax highlighting support.
It is intentionally an installer only: it does not provide an uninstall mode.

Supported editor integrations included in this package:
- Geany 1.x/2.x (Lua lexer based filetype)
- GtkSourceView 2.x through 6.x user language paths
  (Xed, Gedit, Pluma, Mousepad and other GtkSourceView editors use this)
- VS Code / compatible VS Code builds
- Kate / KDE syntax highlighting
- Sublime Text
- Vim
- Neovim
- Emacs
- Micro
- Notepad++ UDL export file
- Code::Blocks keyword/run-command support file

The installer detects what is present and installs only the applicable integration.
It is safe to run again; existing Neptune syntax files are replaced by the current
ones.
