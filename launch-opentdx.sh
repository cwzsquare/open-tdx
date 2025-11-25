#!/bin/bash -e

QEMU=$PWD/qemu-l0/build/qemu-system-x86_64
SEABIOS=$PWD/seabios/out/bios.bin

NPSEAMLDR=$PWD/seam-loader/seam-loader-main-1.5/np-seam-loader/seamldr_src/Projects/Server/Emr/Seamldr/output/ENG_TR_O1/EMR_NP_SEAMLDR_ENG_TR_O1.DBG.bin

TDXMODULE=$PWD/tdx-module/bin/debug/libtdx.so
TDXMODULE_SIGSTRUCT=${TDXMODULE}.sigstruct

KERNEL=linux-l1/arch/x86/boot/bzImage
INITRD=linux-l1/initrd.img-l1

IMG=images/l1.img
QEMU_L1=qemu-l1
KVM_L1=kvm-l1
LINUX_L2=linux-l2
EDK2=edk2
SCRIPTS=scripts
# IMG_DIR=images

run_qemu()
{
    local mem=$1
    local smp=$2
    local ssh_port=$3
    local debug_port=$4

    nested_ssh_port=$((ssh_port + 1))
    nested_debug_port=$((debug_port + 1))
    monitor_port=$((debug_port + 100))

    cmdline="console=ttyS0 root=/dev/sda rw earlyprintk=serial net.ifnames=0 nohibernate debug snd_hda_intel=0"
    
    # 初始化 QEMU 参数数组 (使用数组比 eval 拼接字符串更安全、更清晰)
    qemu_args=()

    # 基础参数
    qemu_args+=(-cpu host -machine q35,kernel_irqchip=split -enable-kvm)
    qemu_args+=(-m ${mem})
    qemu_args+=(-smp ${smp})
    qemu_args+=(-bios ${SEABIOS})

    # Firmware configs
    qemu_args+=(-fw_cfg opt/opentdx.npseamldr,file=${NPSEAMLDR})
    qemu_args+=(-fw_cfg opt/opentdx.tdx_module,file=${TDXMODULE})
    qemu_args+=(-fw_cfg opt/opentdx.seam_sigstruct,file=${TDXMODULE_SIGSTRUCT})

    # Drive
    qemu_args+=(-drive format=raw,file=${IMG})

    # Network
    qemu_args+=(-device virtio-net-pci,netdev=net0)
    qemu_args+=(-netdev user,id=net0,host=10.0.2.10,hostfwd=tcp::${ssh_port}-:22,hostfwd=tcp::${nested_ssh_port}-:10032,hostfwd=tcp::${nested_debug_port}-:1234)

    # Monitor
    qemu_args+=(-monitor tcp:127.0.0.1:${monitor_port},server,nowait)

    # VirtFS (Shared folders)
    qemu_args+=(-virtfs local,path=${QEMU_L1},mount_tag=${QEMU_L1},security_model=passthrough,id=${QEMU_L1})
    qemu_args+=(-virtfs local,path=${KVM_L1},mount_tag=${KVM_L1},security_model=passthrough,id=${KVM_L1})
    qemu_args+=(-virtfs local,path=${LINUX_L2},mount_tag=${LINUX_L2},security_model=passthrough,id=${LINUX_L2})
    qemu_args+=(-virtfs local,path=${SCRIPTS},mount_tag=${SCRIPTS},security_model=passthrough,id=${SCRIPTS})
    qemu_args+=(-virtfs local,path=${EDK2},mount_tag=${EDK2},security_model=passthrough,id=${EDK2})

    # GPU Passthrough Logic
    [ ! -z ${GPU} ] && {
        bdfs=($(lspci | grep -i amd | cut -d' ' -f1))
        [ ${#bdfs[@]} -eq 0 ] && {
            echo "[-] No GPU found"
            exit 1
        }

        # Enable Guest vIOMMU support in Kernel Commandline
        cmdline+=" intel_iommu=on iommu=pt iommu=on"
        
        # Add vIOMMU device to QEMU
        qemu_args+=(-device intel-iommu,intremap=on,caching-mode=on)

        # ---------------------------------------------------------------------
        # [FIX] 解决 "group used in multiple address spaces" 问题
        # 即使 Host 上有多个设备在同一个 Group，我们只直通第一个给 VM。
        # 这样避免了 Guest vIOMMU 试图将同一个物理 Group 拆分到不同虚拟域的冲突。
        # ---------------------------------------------------------------------
        
        # 只取数组中的第一个设备
        target_bdf="${bdfs[0]}" 
        echo "Detected GPU devices: ${bdfs[*]}"
        echo "Passing through ONLY primary device: ${target_bdf} to avoid IOMMU group split conflict."

        # 为这一个设备创建 Root Port 并挂载
        qemu_args+=(-device pcie-root-port,id=port0,chassis=0,slot=0,bus=pcie.0)
        qemu_args+=(-device vfio-pci,host=${target_bdf},bus=port0)
    }

    # Debug
    [ ! -z ${DEBUG} ] && {
        qemu_args+=(-S -gdb tcp::${debug_port})
    }

    # Kernel cmdline
    qemu_args+=(-kernel ${KERNEL} -initrd ${INITRD} -append "${cmdline}")
    qemu_args+=(-nographic)

    # Print the command (optional)
    echo "Starting QEMU..."
    # echo sudo ${QEMU} "${qemu_args[@]}"

    # Execution
    sudo ${QEMU} "${qemu_args[@]}"
}

# Function to show usage information
usage() {
  echo "Usage: $0 [-m <mem>] [-s <smp>] [-p <ssh_port>] [-d <debug_port>]" 1>&2
  echo "Options:" 1>&2
  echo "  -m <mem>              Specify the memory size" 1>&2
  echo "                               - default: 8g" 1>&2
  echo "  -s <smp>              Specify the SMP" >&2
  echo "                               - default: 8" 1>&2
  echo "  -p <ssh_port>         Specify the ssh port for l1/l2" 1>&2
  echo "                         port for l2 will be <ssh_port> + 1" 1>&2
  echo "                               - default: 10032" 1>&2
  echo "  -d <debug_port>       Specify the debug port for l1/l2" 1>&2
  echo "                         port for l2 will be <debug_port> + 1" 1>&2
  echo "                               - default: 1234" 1>&2
  echo "                         monitor port will be <debug_port> + 100" 1>&2
  exit 1
}

mem=8g
smp=8
ssh_port=10032
debug_port=1234

while getopts ":hm:s:p:d:" opt; do
    case $opt in
        h)
            usage
            ;;
        m)
            mem=$OPTARG
            echo "Memory: ${mem}"
            ;;
        s)
            smp=$OPTARG
            echo "SMP: ${smp}"
            ;;
        p)
            ssh_port=$OPTARG
            echo "SSH Port: ${ssh_port}"
            ;;
        d)
            debug_port=$OPTARG
            echo "Debug Port: ${debug_port}"
            ;;
        \?)
            echo "Invalid option: -$OPTARG" >&2
            usage
            ;;
    esac
done

shift $((OPTIND -1))

run_qemu ${mem} ${smp} ${ssh_port} ${debug_port}
