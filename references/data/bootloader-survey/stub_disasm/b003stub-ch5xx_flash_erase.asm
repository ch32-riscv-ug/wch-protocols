# b003stub-ch5xx_flash_erase  (316 bytes)
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
  40:	14050613          	addi	x12,x10,320
  44:	4218                	lw	x14,0(x12)
  46:	425c                	lw	x15,4(x12)
  48:	00052223          	sw	x0,4(x10)
  4c:	62c1                	lui	x5,0x10
  4e:	0d800593          	li	x11,216
  52:	4150                	lw	x12,4(x10)
  54:	0605                	addi	x12,x12,1
  56:	c150                	sw	x12,4(x10)
  58:	fff28613          	addi	x12,x5,-1 # 0xffff
  5c:	8e7d                	and	x12,x12,x15
  5e:	ce11                	beqz	x12,0x7a
  60:	0042d293          	srli	x5,x5,0x4
  64:	ff028613          	addi	x12,x5,-16
  68:	ca4d                	beqz	x12,0x11a
  6a:	02000593          	li	x11,32
  6e:	6605                	lui	x12,0x1
  70:	fe5601e3          	beq	x12,x5,0x52
  74:	08100593          	li	x11,129
  78:	bfe9                	j	0x52
  7a:	fe5763e3          	bltu	x14,x5,0x60
  7e:	40570733          	sub	x14,x14,x5
  82:	00068323          	sb	x0,6(x13)
  86:	4615                	li	x12,5
  88:	a2f0                	fsd	f12,192(x13)
  8a:	4619                	li	x12,6
  8c:	0001                	nop
  8e:	0001                	nop
  90:	a2d0                	fsd	f12,128(x13)
  92:	00668603          	lb	x12,6(x13)
  96:	fe064ee3          	bltz	x12,0x92
  9a:	00068323          	sb	x0,6(x13)
  9e:	00068323          	sb	x0,6(x13)
  a2:	4615                	li	x12,5
  a4:	a2f0                	fsd	f12,192(x13)
  a6:	0001                	nop
  a8:	0001                	nop
  aa:	a2cc                	fsd	f11,128(x13)
  ac:	460d                	li	x12,3
  ae:	83be                	mv	x7,x15
  b0:	00668303          	lb	x6,6(x13)
  b4:	fe034ee3          	bltz	x6,0xb0
  b8:	0103d313          	srli	x6,x7,0x10
  bc:	0ff37313          	zext.b	x6,x6
  c0:	00668223          	sb	x6,4(x13)
  c4:	03a2                	slli	x7,x7,0x8
  c6:	167d                	addi	x12,x12,-1 # 0xfff
  c8:	f665                	bnez	x12,0xb0
  ca:	9796                	add	x15,x15,x5
  cc:	00668603          	lb	x12,6(x13)
  d0:	fe064ee3          	bltz	x12,0xcc
  d4:	00068323          	sb	x0,6(x13)
  d8:	00080337          	lui	x6,0x80
  dc:	04030563          	beqz	x6,0x126
  e0:	137d                	addi	x6,x6,-1 # 0x7ffff
  e2:	4615                	li	x12,5
  e4:	00068323          	sb	x0,6(x13)
  e8:	a2f0                	fsd	f12,192(x13)
  ea:	a2d0                	fsd	f12,128(x13)
  ec:	00668603          	lb	x12,6(x13)
  f0:	fe064ee3          	bltz	x12,0xec
  f4:	00468383          	lb	x7,4(x13)
  f8:	00668603          	lb	x12,6(x13)
  fc:	fe064ee3          	bltz	x12,0xf8
 100:	00468383          	lb	x7,4(x13)
 104:	00668603          	lb	x12,6(x13)
 108:	fe064ee3          	bltz	x12,0x104
 10c:	00068323          	sb	x0,6(x13)
 110:	0013f393          	andi	x7,x7,1
 114:	fc0394e3          	bnez	x7,0xdc
 118:	bf2d                	j	0x52
 11a:	00668303          	lb	x6,6(x13)
 11e:	fe034ee3          	bltz	x6,0x11a
 122:	00068323          	sb	x0,6(x13)
 126:	c15c                	sw	x15,4(x10)
 128:	56fd                	li	x13,-1
 12a:	c114                	sw	x13,0(x10)
 12c:	300027f3          	csrr	x15,mstatus
 130:	0887e793          	ori	x15,x15,136
 134:	30079073          	csrw	mstatus,x15
 138:	8082                	ret
 13a:	0001                	nop
