#!/bin/bash
set -e
pushd kvm-l0
make -C /home/cwz/open-tdx/linux-l0 M=/home/cwz/open-tdx/kvm-l0 clean
make -j$(nproc) -C /home/cwz/open-tdx/linux-l0 M=/home/cwz/open-tdx/kvm-l0
sudo scp -v kvm.ko kvm-intel.ko /lib/modules/$(uname -r)/kernel/arch/x86/kvm/
popd
#sudo modprobe -v -r kvm_intel && sudo modprobe -v kvm_intel open_tdx=1
sudo modprobe -v -r kvm_intel && sudo modprobe -v kvm_intel dump_invalid_vmcs=1
