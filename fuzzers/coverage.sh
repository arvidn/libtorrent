#!/bin/bash


b2 clang build_coverage test-coverage=on stage

function run
{
	mkdir -p profraw/${1}
	LLVM_PROFILE_FILE='profraw/'${1}'/'${1}'-%p.profraw' ./fuzzers/${1} corpus/${1}/ -runs=0 -jobs=10

	llvm-profdata-18 merge -sparse profraw/${1}/${1}-*.profraw -o ${1}.profdata

	mkdir -p coverage-report/${1}
	llvm-cov-18 show -format=html -output-dir=coverage-report/${1} -instr-profile=${1}.profdata ./fuzzers/${1} ../src/ ../include/libtorrent/

	llvm-cov-18 report -instr-profile=${1}.profdata ./fuzzers/${1} ../src/ ../include/libtorrent/
}

for file in fuzzers/*; do
	run $(basename $file)
done
