# b003stub-ch5xx_flash_end  (26 bytes)
# riscv-none-elf-objdump -D -b binary -m riscv:rv32 -M numeric
   0:	400026b7          	lui	x13,0x40002
   4:	80068693          	addi	x13,x13,-2048 # 0x40001800
   8:	00668703          	lb	x14,6(x13)
   c:	fe074ee3          	bltz	x14,0x8
  10:	00068323          	sb	x0,6(x13)
  14:	56fd                	li	x13,-1
  16:	c114                	sw	x13,0(x10)
  18:	8082                	ret
