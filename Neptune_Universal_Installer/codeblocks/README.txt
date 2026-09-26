Neptune syntax support for Code::Blocks

Code::Blocks does not provide one portable user syntax-definition path shared by all
recent releases. The installer therefore installs the Neptune source file and run
configuration under ~/.local/share/neptune-editor-support/codeblocks instead of
modifying internal Code::Blocks files.

If your Code::Blocks build supports a custom lexer/plugin, use the Neptune keyword
set from this package. The standard run command is:

neptune "%f"

Neptune keywords:
var vartable struct if else while for end do return efunc echo cin
true false include Include define type length add remove clear upper lower
contains replace substring read write append exists delete

Operators include: + - * / % = == != < > <= >= @
