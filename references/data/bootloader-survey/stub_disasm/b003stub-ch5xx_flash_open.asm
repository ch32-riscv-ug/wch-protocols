# b003stub-ch5xx_flash_open  (66 bytes)
# riscv-none-elf-objdump -D -b binary -m riscv:rv32 -M numeric
   0:	400026b7          	lui	x13,0x40002
   4:	80068693          	addi	x13,x13,-2048 # 0x40001800
   8:	4711                	li	x14,4
   a:	00e68323          	sb	x14,6(x13)
   e:	4715                	li	x14,5
  10:	00068323          	sb	x0,6(x13)
  14:	00e68323          	sb	x14,6(x13)
  18:	0ff00793          	li	x15,255
  1c:	0001                	nop
  1e:	0001                	nop
  20:	00f68223          	sb	x15,4(x13)
  24:	00668703          	lb	x14,6(x13)
  28:	fe074ee3          	bltz	x14,0x24
  2c:	00f68223          	sb	x15,4(x13)
  30:	00668703          	lb	x14,6(x13)
  34:	fe074ee3          	bltz	x14,0x30
  38:	00068323          	sb	x0,6(x13)
  3c:	56fd                	li	x13,-1
  3e:	c114                	sw	x13,0(x10)
  40:	8082                	ret
