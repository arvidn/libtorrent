#!/bin/bash
set -e

function minimize
{
mkdir corpus/${1}
./fuzzers/${1} -artifact_prefix=./${1}- -merge=1 corpus/${1} prev-corpus/${1}
}

mv corpus prev-corpus
mkdir corpus

for file in fuzzers/*; do
	minimize $(basename $file) &
done

wait

