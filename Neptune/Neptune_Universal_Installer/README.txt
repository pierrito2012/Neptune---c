NEPTUNE C-- UNIVERSAL EDITOR SUPPORT

The installer detects what is installed instead of assuming one exact version.

GtkSourceView:
The Neptune .lang file uses language-definition format 2.0, which is supported
by GtkSourceView 2 through 5. The installer places it into every matching
user language-specs directory it finds, and also detects the GtkSourceView
library linked by Xed.

Geany:
A real custom filetype is installed as filetypes.Neptune.conf and .nep is
registered in filetype_extensions.conf. The backend lexer is Lua because
Geany/Scintilla requires an existing lexer for this style of custom filetype.

Other editors:
VS Code, Kate, Sublime, Vim, Neovim, Emacs and Micro receive their native
syntax definitions.

Notepad++:
A UDL XML is included. Notepad++ is a Windows editor, so the Linux installer
does not claim to install it automatically.

Windows Notepad:
Classic Windows Notepad does not provide a general third-party language
definition system like Geany/GtkSourceView. It cannot be made to do full
Neptune syntax coloring by copying a config file.

Code::Blocks:
The package includes instructions because lexer support varies by Code::Blocks
release and replacing its global lexer configuration would be unsafe.
