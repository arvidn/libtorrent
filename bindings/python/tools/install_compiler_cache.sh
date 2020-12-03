#!/bin/bash

set -e
set -x

# Don't re-install ccache/sccache if it was already installed
if [[ "$OSTYPE" == "linux-gnu"* ]] && [[ -f /usr/bin/ccache ]]
then
  echo Using cache...
  exit 0
elif [[ "$OSTYPE" == "msys" ]] && [[ -f /c/ProgramData/Chocolatey/bin/sccache.exe ]]
then
  echo Using cache...
  exit 0
elif [[ "$OSTYPE" == "darwin"* ]] && [[ -f /usr/local/bin/ccache ]]
then
  echo Using cache...
  exit 0
fi

# Install ccache from Yum on Linux
if [[ "$OSTYPE" == "linux-gnu"* ]] && [[ `getconf LONG_BIT` == "64" ]]
then
  yum -y install ccache
fi

# Install sccache from Chocolatey on Windows
# if [[ "$OSTYPE" == "msys" ]]
# then
#   choco install sccache
# fi

# Install ccache from Homebrew on macOS
# if [[ "$OSTYPE" == "darwin"* ]]
# then
#   brew update
#   brew install ccache
# fi
