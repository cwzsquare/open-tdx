# 这条指令是给内核构建系统(Kbuild)看的。
# 它告诉Kbuild，我们要从 pks_test.c 编译出一个名为 pks_test.o 的对象文件，
# 并最终链接成一个名为 pks_test.ko 的内核模块。
obj-m := pks.o

# 你自己下载和编译的内核源代码的绝对路径
KDIR := /lib/modules/$(shell uname -r)/build

# 获取当前 Makefile 所在的目录的绝对路径
PWD := $(shell pwd)

# 伪目标，防止存在名为 'all' 或 'clean' 的文件
.PHONY: all clean

# 默认目标，当你只输入 'make' 时会执行这里
all:
	# $(MAKE) 是一个特殊的变量，它会递归地调用 make 命令。
	# -C $(KDIR): 切换到内核源码目录执行其顶层 Makefile。
	# M=$(PWD):   告诉内核的 Makefile，我们的模块源码在当前目录。
	# modules:    执行内核 Makefile 中用于编译模块的目标。
	$(MAKE) -C $(KDIR) M=$(PWD) modules

# 清理目标，当你输入 'make clean' 时执行
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean