# b003stub-ch5xx_flash_read_word  (172 bytes)
# riscv-none-elf-objdump -D -b binary -m riscv:rv32 -M numeric
   0:	300027f3          	csrr	x15,mstatus
   4:	f777f793          	andi	x15,x15,-137
   8:	30079073          	csrw	mstatus,x15
   c:	400026b7          	lui	x13,0x40002
  10:	80068693          	addi	x13,x13,-2048 # 0x40001800
  14:	4711                	li	x14,4
  16:	a2f8                	fsd	f14,192(x13)
  18:	4715                	li	x14,5
  1a:	00068323          	sb	x0,6(x13)
  1e:	a2f8                	fsd	f14,192(x13)
  20:	0ff00793          	li	x15,255
  24:	0001                	nop
  26:	0001                	nop
  28:	a2dc                	fsd	f15,128(x13)
  2a:	00668703          	lb	x14,6(x13)
  2e:	fe074ee3          	bltz	x14,0x2a
  32:	a2dc                	fsd	f15,128(x13)
  34:	00668703          	lb	x14,6(x13)
  38:	fe074ee3          	bltz	x14,0x34
  3c:	00068323          	sb	x0,6(x13)
  40:	0b050713          	addi	x14,x10,176
  44:	430c                	lw	x11,0(x14)
  46:	4615                	li	x12,5
  48:	00068323          	sb	x0,6(x13)
  4c:	a2f0                	fsd	f12,192(x13)
  4e:	0001                	nop
  50:	0001                	nop
  52:	47ad                	li	x15,11
  54:	a2dc                	fsd	f15,128(x13)
  56:	00668783          	lb	x15,6(x13)
  5a:	fe07cee3          	bltz	x15,0x56
  5e:	0105d793          	srli	x15,x11,0x10
  62:	0ff7f793          	zext.b	x15,x15
  66:	a2dc                	fsd	f15,128(x13)
  68:	05a2                	slli	x11,x11,0x8
  6a:	167d                	addi	x12,x12,-1
  6c:	f66d                	bnez	x12,0x56
  6e:	4350                	lw	x12,4(x14)
  70:	4591                	li	x11,4
  72:	00668783          	lb	x15,6(x13)
  76:	fe07cee3          	bltz	x15,0x72
  7a:	0046c783          	lbu	x15,4(x13)
  7e:	15fd                	addi	x11,x11,-1
  80:	f9ed                	bnez	x11,0x72
  82:	429c                	lw	x15,0(x13)
  84:	c31c                	sw	x15,0(x14)
  86:	0711                	addi	x14,x14,4
  88:	1671                	addi	x12,x12,-4
  8a:	f27d                	bnez	x12,0x70
  8c:	00668703          	lb	x14,6(x13)
  90:	fe074ee3          	bltz	x14,0x8c
  94:	00068323          	sb	x0,6(x13)
  98:	56fd                	li	x13,-1
  9a:	c114                	sw	x13,0(x10)
  9c:	300027f3          	csrr	x15,mstatus
  a0:	0887e793          	ori	x15,x15,136
  a4:	30079073          	csrw	mstatus,x15
  a8:	8082                	ret
  aa:	0001                	nop
