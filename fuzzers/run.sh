#!/bin/bash

JOBS=$(nproc)

function run
{
	mkdir -p corpus/${1}
	nice ./fuzzers/${1} -max_total_time=900 -jobs=${JOBS} -timeout=10 -artifact_prefix=./${1}- corpus/${1}
}

if [ $# -gt 0 ]; then
	run "$1"
else
	while true; do
		for file in fuzzers/*; do
			run "$(basename "$file")"
		done
	done
fi
