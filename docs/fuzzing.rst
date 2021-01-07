==================
fuzzing libtorrent
==================

.. include:: header.rst

.. contents:: Table of contents
  :depth: 2
  :backlinks: none

Libtorrent comes with a set of fuzzers. They are not included in the distribution
tar ball, instead download the `repository snapshot`_ or clone the repository_.

The fuzzers can be found in the `fuzzers` subdirectory and come with a `Jamfile`
to build them, and a `run.sh` bash script to run them.

.. _`repository snapshot`: https://github.com/arvidn/libtorrent/releases
.. _repository: https://github.com/arvidn/libtorrent

building
--------

The fuzzers use clang's libFuzzer, which means they can only be built with clang.
Clang must be configured in your `user-config.jam`, for example::

	using clang : 7 : clang++-7 ;

When building, you most likely want to stage the resulting binaries into a
well known location. Invoke `b2` like this::

	b2 clang stage

This will build and stage all fuzzers into the `fuzzers/fuzzers` directory.

corpus
------

Fuzzers work best if they have a relevant seed corpus of example inputs. You
can either generate one using `fuzzers/tools/generate_initial_corpus.py` or download
the `corpus.zip` from the github `releases page`_.

To run the script to generate initial corpus, run it with `fuzzers` as the
current working directory, like this::

	python tools/generate_initial_corpus.py

The corpus should be placed in the `fuzzers` directory, which should also be the
current working directory when invoking the fuzzer binaries.

.. _`releases page`: https://github.com/arvidn/libtorrent/releases

running fuzzers
---------------

The `run.sh` script will run all fuzzers in parallel for 48 hours. It can easily
be tweaked and mostly serve as an example of how to invoke them.

large and small fuzzers
-----------------------

Since APIs can have different complexity, fuzz targets will also explore
code of varying complexity. Some fuzzers cover a very small amount of code
(e.g. `parse_int`) where other fuzz targets cover very large amount of code and
can potentially go very deep into call stacks (e.g. `torrent_info`).

Small fuzz targets can fairly quickly exhaust all possible code paths and have
quite limited utility after that, other than as regression tests. When putting
a lot of CPU into long running fuzzing, it is better spent on large fuzz targets.

For this reason, there's another alias in the `Jamfile` to only build and stage
large fuzz targets. Call `b2` like this::

	b2 clang stage-large

fast+slow
---------

When building an initial corpus, it can be useful to quickly build a corpus with
a large code coverage. To speed up this process, you can build the fuzzers
without sanitizers, asserts and invariant checks. This won't find as many errors,
but build a good corpus which can then be run against a fully instrumented
fuzzer.

To build the fuzzers in this "fast" mode, there's a build variant `build_coverage`.
Invoke `b2` like this::

	b2 clang stage build_coverage

For more details on "fast + slow" see `Paul Dreik's talk`_.

.. _`Paul Dreik's talk`: https://youtu.be/e_Oc9SkCo5s?t=1679

sharing corpora
---------------

Before sharing your fuzz corpus, it should be minimized. There is a script
called `minimize.sh` which moves `corpus` to `prev-corpus` and copies over
a minimized set of inputs to a new `corpus` directory.

