#!/bin/bash

# VFIO PTE 验证测试脚本

PROC_FILE="/proc/verify_pte"

# 检查模块是否已加载
check_module() {
    if ! lsmod | grep -q verify_pte; then
        echo "错误: verify_pte 模块未加载"
        echo "请先运行: sudo insmod verify_pte.ko"
        exit 1
    fi
    echo "✓ verify_pte 模块已加载"
}

# 检查 /proc 文件是否存在
check_proc_file() {
    if [ ! -f "$PROC_FILE" ]; then
        echo "错误: $PROC_FILE 不存在"
        exit 1
    fi
    echo "✓ $PROC_FILE 存在"
}

# 验证地址
verify_address() {
    local addr=$1
    
    if [ -z "$addr" ]; then
        echo "用法: $0 <虚拟地址(十六进制)>"
        echo "示例: $0 0x7f1234567000"
        exit 1
    fi
    
    # 移除可能的 0x 前缀（如果用户输入了两次）
    addr=$(echo $addr | sed 's/^0x//')
    addr="0x$addr"
    
    echo "设置目标地址: $addr"
    echo "$addr" > "$PROC_FILE"
    
    if [ $? -ne 0 ]; then
        echo "错误: 无法写入地址"
        exit 1
    fi
    
    echo ""
    echo "=== PTE 状态 ==="
    cat "$PROC_FILE"
    echo ""
    
    # 检查结果
    if cat "$PROC_FILE" | grep -q "CLEARED (kernel page)"; then
        echo "✓ 成功: _PAGE_USER 位已被清除（内核页）"
        return 0
    elif cat "$PROC_FILE" | grep -q "SET (user page)"; then
        echo "✗ 失败: _PAGE_USER 位仍然设置（用户页）"
        return 1
    else
        echo "? 无法确定状态"
        return 2
    fi
}

# 从内核日志中提取地址
extract_addresses_from_dmesg() {
    echo "=== 从内核日志中提取 VFIO 地址 ==="
    dmesg | grep "VFIO: Clearing _PAGE_USER" | tail -5
    echo ""
    echo "提示: 使用上述地址进行验证"
}

# 主函数
main() {
    if [ "$1" == "--help" ] || [ "$1" == "-h" ]; then
        echo "VFIO PTE 验证工具"
        echo ""
        echo "用法:"
        echo "  $0 <虚拟地址>          - 验证指定地址的 PTE"
        echo "  $0 --dmesg             - 从内核日志提取 VFIO 地址"
        echo "  $0 --check              - 检查模块和文件状态"
        echo ""
        exit 0
    fi
    
    if [ "$1" == "--dmesg" ]; then
        extract_addresses_from_dmesg
        exit 0
    fi
    
    if [ "$1" == "--check" ]; then
        check_module
        check_proc_file
        exit 0
    fi
    
    check_module
    check_proc_file
    verify_address "$1"
}

main "$@"

