REM this is just to get libtorrent built
bjam -j8 msvc-12.0 boost=source invariant-checks=off asserts=off link=shared boost-link=shared test_primitives linkflags=/profile deprecated-functions=off

Vsinstr -coverage ..\bin\msvc-12.0\debug\asserts-off\boost-link-shared\boost-source\debug-iterators-on\deprecated-functions-off\export-extra-on\invariant-checks-off\threading-multi\torrent.dll @vsinstr_excludes.rsp

REM prepare for running all the tests again
del /s /q bin
del /q test.coverage

REM add vsperfmon to the path
set path=%path%;c:\Program Files (x86)\Microsoft Visual Studio 12.0\Team Tools\Performance Tools

Start vsperfmon -coverage -output:test.coverage

REM Now we run all the unit tests to record test coverage
bjam -j8 msvc-12.0 boost=source invariant-checks=off asserts=off link=shared boost-link=shared linkflags=/profile deprecated-functions=off

vsperfcmd -shutdown

