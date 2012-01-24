if did_filetype()
  finish
endif
if getline(1) =~ '^yux+apa mapfile v\.2'
  setfiletype yuxtapa
  set nowrap
endif
