if exists("b:current_syntax")
  finish
endif
syn case match
syn keyword neptuneDeclaration var vartable struct
syn keyword neptuneControl if else while for end do return include
syn keyword neptuneFunction efunc echo cin
syn keyword neptuneConstant true false
syn match neptuneNumber "\<\d\+\(\.\d\+\)\?\>"
syn match neptuneOperator "==\|!=\|>=\|<=\|[+*/%=<>-]"
syn match neptuneComment "#.*$"
syn region neptuneString start=/"/ skip=/\\./ end=/"/
hi def link neptuneDeclaration Type
hi def link neptuneControl Keyword
hi def link neptuneFunction Function
hi def link neptuneConstant Constant
hi def link neptuneNumber Number
hi def link neptuneOperator Operator
hi def link neptuneComment Comment
hi def link neptuneString String
let b:current_syntax = "neptune"
