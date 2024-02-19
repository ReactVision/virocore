#!/usr/bin/env bash

set -e

echo '========================================================================='
echo 'Cleaning :viroreact'
echo '========================================================================='
cd android &&
./gradlew clean

echo '========================================================================='
echo 'Building :viroreact'
echo '========================================================================='
./gradlew :viroreact:check
./gradlew :viroreact:assembleRelease

echo '========================================================================='
echo 'Checking for build artifacts'
echo '========================================================================='
if [ ! -f viroreact/build/outputs/aar/viroreact-release.aar ]; then
    echo -e "Unable to find viroreact release output!"
    exit 1
fi
