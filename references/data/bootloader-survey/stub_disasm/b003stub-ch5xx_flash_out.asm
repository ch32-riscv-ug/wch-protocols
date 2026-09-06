# b003stub-ch5xx_flash_out  (32 bytes)
# riscv-none-elf-objdump -D -b binary -m riscv:rv32 -M numeric
   0:	400026b7          	lui	x13,0x40002
   4:	80068693          	addi	x13,x13,-2048 # 0x40001800
   8:	02450713          	addi	x14,x10,36
   c:	431c                	lw	x15,0(x14)
   e:	00668703          	lb	x14,6(x13)
  12:	fe074ee3          	bltz	x14,0xe
  16:	00f68223          	sb	x15,4(x13)
  1a:	56fd                	li	x13,-1
  1c:	c114                	sw	x13,0(x10)
  1e:	8082                	ret
