# b003stub-ch5xx_flash_in  (28 bytes)
# riscv-none-elf-objdump -D -b binary -m riscv:rv32 -M numeric
   0:	400026b7          	lui	x13,0x40002
   4:	80068693          	addi	x13,x13,-2048 # 0x40001800
   8:	00668703          	lb	x14,6(x13)
   c:	fe074ee3          	bltz	x14,0x8
  10:	0046c783          	lbu	x15,4(x13)
  14:	c15c                	sw	x15,4(x10)
  16:	56fd                	li	x13,-1
  18:	c114                	sw	x13,0(x10)
  1a:	8082                	ret
