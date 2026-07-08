#!/bin/bash
set -e

function minimize
{
mkdir corpus/${1}
nice ./fuzzers/${1} -artifact_prefix=./${1}- -merge=1 minimized-corpus/${1} corpus/${1}
}

mkdir minimized-corpus

for file in fuzzers/*; do
	minimize $(basename $file) &
done

wait

