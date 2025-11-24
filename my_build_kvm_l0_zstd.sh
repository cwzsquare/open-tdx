#!/bin/bash
set -e
pushd kvm-l0
make -C /home/cwz/open-tdx/linux-l0 M=/home/cwz/open-tdx/kvm-l0 clean
make -j$(nproc) -C /home/cwz/open-tdx/linux-l0 M=/home/cwz/open-tdx/kvm-l0

# some host config may have zstd for modprobe to use
zstd -f --rm kvm.ko kvm-intel.ko

sudo scp -v kvm.ko.zst kvm-intel.ko.zst /lib/modules/$(uname -r)/kernel/arch/x86/kvm/
popd
#sudo modprobe -v -r kvm_intel && sudo modprobe -v kvm_intel open_tdx=1
sudo modprobe -v -r kvm_intel && sudo modprobe -v kvm_intel dump_invalid_vmcs=1
