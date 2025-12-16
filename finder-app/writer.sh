#!/bin/bash

# Check for correct number of arguments
if [ $# -ne 2 ]; then
	echo "Error: Two arguments required: <writefile> <writestr>" >&2
	exit 1
fi

writefile=$1
writestr=$2

# Create directory if it does not exist
dirpath=$(dirname "$writefile")
if ! mkdir -p "$dirpath" 2>/dev/null; then
	echo "Error: Could not create directory '$dirpath'" >&2
	exit 1
fi

# Write string to file (overwrite if exists)
if ! echo "$writestr" > "$writefile"; then
	echo "Error: Could not write to file '$writefile'" >&2
	exit 1
fi

exit 0
