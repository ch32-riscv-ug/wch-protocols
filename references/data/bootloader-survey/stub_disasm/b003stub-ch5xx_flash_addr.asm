# b003stub-ch5xx_flash_addr  (96 bytes)
# riscv-none-elf-objdump -D -b binary -m riscv:rv32 -M numeric
   0:	06450693          	addi	x13,x10,100
   4:	4298                	lw	x14,0(x13)
   6:	42dc                	lw	x15,4(x13)
   8:	400026b7          	lui	x13,0x40002
   c:	80068693          	addi	x13,x13,-2048 # 0x40001800
  10:	462d                	li	x12,11
  12:	0bf77593          	andi	x11,x14,191
  16:	00c59463          	bne	x11,x12,0x1e
  1a:	4615                	li	x12,5
  1c:	a831                	j	0x38
  1e:	00068323          	sb	x0,6(x13)
  22:	4615                	li	x12,5
  24:	a2f0                	fsd	f12,192(x13)
  26:	4619                	li	x12,6
  28:	a2d0                	fsd	f12,128(x13)
  2a:	00668603          	lb	x12,6(x13)
  2e:	fe064ee3          	bltz	x12,0x2a
  32:	00068323          	sb	x0,6(x13)
  36:	460d                	li	x12,3
  38:	00068323          	sb	x0,6(x13)
  3c:	4595                	li	x11,5
  3e:	a2ec                	fsd	f11,192(x13)
  40:	a2d8                	fsd	f14,128(x13)
  42:	00668703          	lb	x14,6(x13)
  46:	fe074ee3          	bltz	x14,0x42
  4a:	0107d593          	srli	x11,x15,0x10
  4e:	0ff5f593          	zext.b	x11,x11
  52:	a2cc                	fsd	f11,128(x13)
  54:	07a2                	slli	x15,x15,0x8
  56:	167d                	addi	x12,x12,-1
  58:	f66d                	bnez	x12,0x42
  5a:	56fd                	li	x13,-1
  5c:	c114                	sw	x13,0(x10)
  5e:	8082                	ret
