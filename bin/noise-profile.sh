#!/bin/bash
if [ "$#" -ne 1 ]; then
    echo "usage: noise-profile.sh <raw image file>"
    exit -1
fi
fname=$(./vkdt-cli -g examples/noiseprofile.cfg --config param:i-raw:01:filename:$1 | grep nprof | cut -d\' -f2)
mkdir -p data/nprof
mv "$fname" data/nprof/
./vkdt-cli -g examples/noisecheck.cfg --filename "$fname" --config param:i-raw:01:filename:$1

