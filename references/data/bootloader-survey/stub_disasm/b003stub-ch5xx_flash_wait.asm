# b003stub-ch5xx_flash_wait  (104 bytes)
# riscv-none-elf-objdump -D -b binary -m riscv:rv32 -M numeric
   0:	400026b7          	lui	x13,0x40002
   4:	80068693          	addi	x13,x13,-2048 # 0x40001800
   8:	06c50713          	addi	x14,x10,108
   c:	4318                	lw	x14,0(x14)
   e:	4310                	lw	x12,0(x14)
  10:	00668703          	lb	x14,6(x13)
  14:	fe074ee3          	bltz	x14,0x10
  18:	00068323          	sb	x0,6(x13)
  1c:	c239                	beqz	x12,0x62
  1e:	167d                	addi	x12,x12,-1
  20:	4795                	li	x15,5
  22:	00068323          	sb	x0,6(x13)
  26:	4715                	li	x14,5
  28:	0001                	nop
  2a:	0001                	nop
  2c:	00e68323          	sb	x14,6(x13)
  30:	00f68223          	sb	x15,4(x13)
  34:	00668703          	lb	x14,6(x13)
  38:	fe074ee3          	bltz	x14,0x34
  3c:	00468783          	lb	x15,4(x13)
  40:	00668703          	lb	x14,6(x13)
  44:	fe074ee3          	bltz	x14,0x40
  48:	00468783          	lb	x15,4(x13)
  4c:	00668703          	lb	x14,6(x13)
  50:	fc0740e3          	bltz	x14,0x10
  54:	00068323          	sb	x0,6(x13)
  58:	0017f713          	andi	x14,x15,1
  5c:	f371                	bnez	x14,0x20
  5e:	0705                	addi	x14,x14,1
  60:	c158                	sw	x14,4(x10)
  62:	56fd                	li	x13,-1
  64:	c114                	sw	x13,0(x10)
  66:	8082                	ret
