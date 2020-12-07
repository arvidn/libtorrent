#!/bin/bash

set -e
set -x

# Get number of CPU cores and Python version
cores=$(python -c "import multiprocessing; print(multiprocessing.cpu_count(), end='')")
version=$(python -c "import sys; print(sys.version[:3].replace('.', ''), end='')")
model=$(python -c "import sys; print('x64' if sys.maxsize > 2**32 else 'x32', end='')")

# Write Python config to fix bug where Boost guesses wrong paths
python $1/bindings/python/tools/generate_boost_config.py ~/user-config.jam

# Don't re-build and re-install Boost if it was already installed
# If both Boost.Boost and Boost.Python are already installed, there isn't any need for additional Boost.System check
if [[ "$OSTYPE" == "msys" ]] && [[ -f /c/Boost/bin/b2.exe ]] && [[ -f /c/Boost/lib/libboost_python$version-mt-$model.lib ]]
then
  echo Using cache...
  rm -f /c/Boost/lib/boost_system.lib /c/Boost/lib/boost_python$version.lib
  ln /c/Boost/lib/libboost_system-mt-$model.lib /c/Boost/lib/boost_system.lib
  ln /c/Boost/lib/libboost_python$version-mt-$model.lib /c/Boost/lib/boost_python$version.lib
  exit 0
elif [[ "$OSTYPE" == "darwin"* ]] && [[ -f /usr/local/bin/b2 ]] && [[ -f /usr/local/lib/libboost_python$version-mt.dylib ]]
then
  echo Using cache...
  exit 0
fi

# Install Boost from Homebrew on macOS if it was not already installed
if [[ "$OSTYPE" == "darwin"* ]] && [[ ! -f /usr/local/bin/b2 ]]
then
  brew update
  brew install boost boost-build
fi

# Download Boost sources if they are not already downloaded
# They are needed for Boost.Python libraries, even on macOS
if [[ ! -d /tmp/boost_1_74_0 ]]
then
  curl -L https://dl.bintray.com/boostorg/release/1.74.0/source/boost_1_74_0.tar.gz -o /tmp/boost.tar.gz
  tar xzf /tmp/boost.tar.gz -C /tmp
fi

# Install Boost.Python as root and use same config as Boost from Homebrew on macOS
# On other systems, just use normal tagged layout for whole Boost installed
if [[ "$OSTYPE" == "darwin"* ]]
then
  root=sudo
  threading=threading=multi,single
  link=link=shared,static
  layout=--layout=tagged-1.66
else
  layout=--layout=tagged
fi

# Use MSVC and specific prefixes for Boost on Windows
if [[ "$OSTYPE" == "msys" ]]
then
  toolset=toolset=msvc
  prefix=--prefix=C:/Boost
fi

# Don't run on macOS because it is already installed from Homebrew
# Also don't run if it was already installed on previous run
if [[ "$OSTYPE" != "darwin"* ]] && ! ([[ -f /usr/local/bin/b2 ]] || [[ -f /c/Boost/bin/b2.exe ]])
then
  # Install Boost.System
  cd /tmp/boost_1_74_0
  rm -f project-config.jam
  ./bootstrap.sh --with-libraries=system $prefix
  ./b2 install release $toolset $threading $link $layout $prefix -j$cores

  # Install Boost.Build
  cd /tmp/boost_1_74_0/tools/build
  ./bootstrap.sh
  ./b2 install release $toolset -j$cores


  # Create boost-build.jam file
  # This is needed because Boost Build since 1.74 does not auto-generate it
  if [[ "$OSTYPE" == "msys" ]]
  then
    printf "boost-build src/kernel ;\n" > /c/Boost/share/boost-build/boost-build.jam
  else
    printf "boost-build src/kernel ;\n" > /usr/local/share/boost-build/boost-build.jam
  fi
fi

# Install Boost.Python
# Check if this version was already installed is not needed, because it happens at the start of the file
cd /tmp/boost_1_74_0
rm -f project-config.jam
./bootstrap.sh --with-libraries=python --with-python=python3 $prefix
$root ./b2 install release $toolset $threading $link $layout $prefix -j$cores

# Link Boost.System and Boost.Python libraries to correct name
if [[ "$OSTYPE" == "linux-gnu"* ]]
then
  rm -f /usr/local/lib/libboost_system.a /usr/local/lib/libboost_system.so
  rm -f /usr/local/lib/libboost_python$version.a /usr/local/lib/libboost_python$version.so
  ln /usr/local/lib/libboost_system-mt-$model.a /usr/local/lib/libboost_system.a
  ln /usr/local/lib/libboost_system-mt-$model.so /usr/local/lib/libboost_system.so
  ln /usr/local/lib/libboost_python$version-mt-$model.a /usr/local/lib/libboost_python$version.a
  ln /usr/local/lib/libboost_python$version-mt-$model.so /usr/local/lib/libboost_python$version.so
elif [[ "$OSTYPE" == "msys" ]]
then
  rm -f /c/Boost/lib/boost_system.lib /c/Boost/lib/boost_python$version.lib
  ln /c/Boost/lib/libboost_system-mt-$model.lib /c/Boost/lib/boost_system.lib
  ln /c/Boost/lib/libboost_python$version-mt-$model.lib /c/Boost/lib/boost_python$version.lib
fi

# Fix Boost.Python library ID on macOS
if [[ "$OSTYPE" == "darwin"* ]]
then
  $root install_name_tool /usr/local/lib/libboost_python$version-mt.dylib -id /usr/local/lib/libboost_python$version-mt.dylib
fi
