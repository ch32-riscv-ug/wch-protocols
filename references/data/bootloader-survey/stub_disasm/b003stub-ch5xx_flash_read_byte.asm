# b003stub-ch5xx_flash_read_byte  (164 bytes)
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
  40:	0a850713          	addi	x14,x10,168
  44:	430c                	lw	x11,0(x14)
  46:	4615                	li	x12,5
  48:	00068323          	sb	x0,6(x13)
  4c:	a2f0                	fsd	f12,192(x13)
  4e:	47ad                	li	x15,11
  50:	a2dc                	fsd	f15,128(x13)
  52:	00668783          	lb	x15,6(x13)
  56:	fe07cee3          	bltz	x15,0x52
  5a:	0105d793          	srli	x15,x11,0x10
  5e:	0ff7f793          	zext.b	x15,x15
  62:	a2dc                	fsd	f15,128(x13)
  64:	05a2                	slli	x11,x11,0x8
  66:	167d                	addi	x12,x12,-1
  68:	f66d                	bnez	x12,0x52
  6a:	4350                	lw	x12,4(x14)
  6c:	00668783          	lb	x15,6(x13)
  70:	fe07cee3          	bltz	x15,0x6c
  74:	0046c583          	lbu	x11,4(x13)
  78:	0046c583          	lbu	x11,4(x13)
  7c:	a30c                	fsd	f11,0(x14)
  7e:	0705                	addi	x14,x14,1
  80:	167d                	addi	x12,x12,-1
  82:	f66d                	bnez	x12,0x6c
  84:	00668703          	lb	x14,6(x13)
  88:	fe074ee3          	bltz	x14,0x84
  8c:	00068323          	sb	x0,6(x13)
  90:	56fd                	li	x13,-1
  92:	c114                	sw	x13,0(x10)
  94:	300027f3          	csrr	x15,mstatus
  98:	0887e793          	ori	x15,x15,136
  9c:	30079073          	csrw	mstatus,x15
  a0:	8082                	ret
  a2:	0001                	nop
