#!/bin/bash

identifier=dev
commit=$(git log -1 --format=format:%h)

regex="s/version='(.*)',/version='\1-$identifier+$commit',/g"

if [[ "$OSTYPE" == "darwin"* ]]
then
  sed -i '' -E $regex $1
else
  sed -E $regex -i $1
fi
