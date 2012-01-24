" Vim syntax file
" Language: yuxtapa map files

if exists("b:current_syntax")
  finish
endif

" Validate parts of the header:
syn match headThree "\%3l\d\+"
syn match headFour "\%4l[01]$"
syn match headFive "\%5l\(out\|dun\|com\)"

" Match map symbols only after line 5:
syn match greenSyms "\%>5l[\.\"T]"
syn match blueSyms "\%>5l[\~]"
syn match brownSyms "\%>5l[+\\,]"
syn match graySyms "\%>5l[_; ]"
syn match darkgraySyms "\%>5l[#HI|-]"
syn match errorSyms "\%>5l[^\.\"T\~+\\,_;#HI|\- ]"

hi def link headThree Constant
hi def link headFour Constant
hi def link headFive Constant
hi def greenSyms ctermfg=darkgreen
hi def blueSyms ctermfg=darkblue
hi def brownSyms ctermfg=brown
hi def graySyms ctermfg=gray
hi def darkgraySyms ctermfg=darkgray
hi def link errorSyms Error

let b:current_syntax = "yuxtapa"

