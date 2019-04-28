#!/bin/bash

function run
{
# run for 48 hours
./fuzzers/${1} -max_total_time=172800 -timeout=10 -artifact_prefix=./${1}- corpus/${1}
}

for file in fuzzers/*; do
	run $(basename $file) &
done

wait
