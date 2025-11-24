savedcmd_verify_pte.mod := printf '%s\n'   verify_pte.o | awk '!x[$$0]++ { print("./"$$0) }' > verify_pte.mod
