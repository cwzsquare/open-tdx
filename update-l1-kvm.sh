#!/bin/bash
./common.sh -t kvm -l l1

pushd kvm-l1 >/dev/null
./build.sh
popd >/dev/null