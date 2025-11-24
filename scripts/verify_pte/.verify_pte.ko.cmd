savedcmd_verify_pte.ko := ld -r -m elf_x86_64 -z noexecstack --build-id=sha1  -T /home/cwz/open-tdx/linux-l1/scripts/module.lds -o verify_pte.ko verify_pte.o verify_pte.mod.o .module-common.o
