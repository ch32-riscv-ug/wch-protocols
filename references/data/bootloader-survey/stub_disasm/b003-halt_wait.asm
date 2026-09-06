# b003-halt_wait  (10 bytes)
# riscv-none-elf-objdump -D -b binary -m riscv:rv32 -M numeric
   0:	4681                	li	x13,0
   2:	c194                	sw	x13,0(x11)
   4:	56fd                	li	x13,-1
   6:	c114                	sw	x13,0(x10)
   8:	8082                	ret
