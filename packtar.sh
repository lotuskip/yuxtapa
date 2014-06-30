#!/bin/sh
# Script to create yuxtapa-XX.tar.gz package.
ver=$(grep --color=never "VERSION =" Makefile | sed 's/VERSION = //')
cd ..
tar -czf yuxtapa-$ver.tar.gz yuxtapa/Makefile yuxtapa/LICENSE yuxtapa/manual yuxtapa/README \
	yuxtapa/server/*[^o] yuxtapa/client/*[^o] yuxtapa/common/*[^o] \
	yuxtapa/testes/*.cpp yuxtapa/testes/README yuxtapa/vhist.html yuxtapa/tmplates
mv -f yuxtapa-$ver.tar.gz yuxtapa
cd yuxtapa
