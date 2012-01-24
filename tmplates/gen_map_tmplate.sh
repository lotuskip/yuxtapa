#!/bin/sh
#Script to generate an empty yuxtapa map file. Usage:
# ./gen_map_tmplate.sh [size of map, integer]
size=100
if test $# -gt 0; then
	size=$1
	if test $size -lt 70 -o $size -gt 365; then
		echo "Invalid mapsize: $size (must be 70-365)" >&2
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
