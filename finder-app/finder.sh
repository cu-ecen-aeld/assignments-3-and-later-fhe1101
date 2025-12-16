#!/bin/bash

# Check for correct number of arguments
if [ $# -ne 2 ]; then
	echo "Error: Two arguments required: <filesdir> <searchstr>" >&2
	exit 1
fi

filesdir=$1
searchstr=$2

# Check if filesdir is a directory
if [ ! -d "$filesdir" ]; then
	echo "Error: '$filesdir' is not a directory" >&2
	exit 1
fi

# Count number of files (recursively)
num_files=$(find "$filesdir" -type f | wc -l)

# Count number of matching lines
num_matches=$(grep -r --binary-files=without-match -F "$searchstr" "$filesdir" 2>/dev/null | wc -l)

echo "The number of files are $num_files and the number of matching lines are $num_matches"
exit 0
