# b003stub-ch5xx_flash_begin  (40 bytes)
# riscv-none-elf-objdump -D -b binary -m riscv:rv32 -M numeric
   0:	400026b7          	lui	x13,0x40002
   4:	80068693          	addi	x13,x13,-2048 # 0x40001800
   8:	02c50713          	addi	x14,x10,44
   c:	431c                	lw	x15,0(x14)
   e:	00068323          	sb	x0,6(x13)
  12:	4715                	li	x14,5
  14:	00e68323          	sb	x14,6(x13)
  18:	0001                	nop
  1a:	0001                	nop
  1c:	00f68223          	sb	x15,4(x13)
  20:	56fd                	li	x13,-1
  22:	c114                	sw	x13,0(x10)
  24:	8082                	ret
  26:	0001                	nop
