#!/bin/bash
#Script to generate an empty yuxtapa map file. Extremely slow, but does the
#job (no bashisms; way faster with dash, zsh, and probably others).
#Usage:
# ./gen_map_tmplate.sh [size of map, integer; default 100]
#
size=100
if test $# -gt 0; then
	size=$1
	if test $size -lt 42 -o $size -gt 511; then
		echo "Invalid mapsize: $size (must be 42-511)" >&2
		exit 1
	fi
fi
echo "yux+apa mapfile v.2"
echo "empty map"
echo $size
echo "0"
echo "out"
i=0
while test $i -lt $size; do
	echo -n "H"
	i=$((i+1))
done
echo ""
size=$((size-2))
i=0
while test $i -lt $size; do
	echo -n "H"
	i=$((i+1))
	j=0
	while test $j -lt $size; do
		echo -n "."
		j=$((j+1))
	done
	echo "H"
done
size=$((size+2))
i=0
while test $i -lt $size; do
	echo -n "H"
	i=$((i+1))
done
echo ""
