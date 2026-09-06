# linke-flashloader-v2  (512 bytes)
# riscv-none-elf-objdump -D -b binary -m riscv:rv32 -M numeric
   0:	00157793          	andi	x15,x10,1
   4:	1141                	addi	x2,x2,-16
   6:	cf99                	beqz	x15,0x24
   8:	456706b7          	lui	x13,0x45670
   c:	400227b7          	lui	x15,0x40022
  10:	12368693          	addi	x13,x13,291 # 0x45670123
  14:	cdef9737          	lui	x14,0xcdef9
  18:	c3d4                	sw	x13,4(x15)
  1a:	9ab70713          	addi	x14,x14,-1621 # 0xcdef89ab
  1e:	c3d8                	sw	x14,4(x15)
  20:	d3d4                	sw	x13,36(x15)
  22:	d3d8                	sw	x14,36(x15)
  24:	00257793          	andi	x15,x10,2
  28:	c795                	beqz	x15,0x54
  2a:	400227b7          	lui	x15,0x40022
  2e:	4b98                	lw	x14,16(x15)
  30:	66ad                	lui	x13,0xb
  32:	40003837          	lui	x16,0x40003
  36:	00476713          	ori	x14,x14,4
  3a:	cb98                	sw	x14,16(x15)
  3c:	4b98                	lw	x14,16(x15)
  3e:	aaa68693          	addi	x13,x13,-1366 # 0xaaaa
  42:	04076713          	ori	x14,x14,64
  46:	cb98                	sw	x14,16(x15)
  48:	47d8                	lw	x14,12(x15)
  4a:	8b05                	andi	x14,x14,1
  4c:	eb61                	bnez	x14,0x11c
  4e:	4b98                	lw	x14,16(x15)
  50:	9b6d                	andi	x14,x14,-5
  52:	cb98                	sw	x14,16(x15)
  54:	00457793          	andi	x15,x10,4
  58:	cba9                	beqz	x15,0xaa
  5a:	0ff60793          	addi	x15,x12,255
  5e:	83a1                	srli	x15,x15,0x8
  60:	c02e                	sw	x11,0(x2)
  62:	682d                	lui	x16,0xb
  64:	7681                	lui	x13,0xfffe0
  66:	c43e                	sw	x15,8(x2)
  68:	000208b7          	lui	x17,0x20
  6c:	400227b7          	lui	x15,0x40022
  70:	40003337          	lui	x6,0x40003
  74:	aaa80813          	addi	x16,x16,-1366 # 0xaaaa
  78:	16fd                	addi	x13,x13,-1 # 0xfffdffff
  7a:	4b98                	lw	x14,16(x15)
  7c:	01176733          	or	x14,x14,x17
  80:	cb98                	sw	x14,16(x15)
  82:	4702                	lw	x14,0(x2)
  84:	cbd8                	sw	x14,20(x15)
  86:	4b98                	lw	x14,16(x15)
  88:	04076713          	ori	x14,x14,64
  8c:	cb98                	sw	x14,16(x15)
  8e:	47d8                	lw	x14,12(x15)
  90:	8b05                	andi	x14,x14,1
  92:	eb41                	bnez	x14,0x122
  94:	4b98                	lw	x14,16(x15)
  96:	8f75                	and	x14,x14,x13
  98:	cb98                	sw	x14,16(x15)
  9a:	4702                	lw	x14,0(x2)
  9c:	10070713          	addi	x14,x14,256
  a0:	c03a                	sw	x14,0(x2)
  a2:	4722                	lw	x14,8(x2)
  a4:	177d                	addi	x14,x14,-1
  a6:	c43a                	sw	x14,8(x2)
  a8:	fb69                	bnez	x14,0x7a
  aa:	00857793          	andi	x15,x10,8
  ae:	cbd5                	beqz	x15,0x162
  b0:	0ff60793          	addi	x15,x12,255
  b4:	c02e                	sw	x11,0(x2)
  b6:	83a1                	srli	x15,x15,0x8
  b8:	c43e                	sw	x15,8(x2)
  ba:	40022737          	lui	x14,0x40022
  be:	4b1c                	lw	x15,16(x14)
  c0:	66c1                	lui	x13,0x10
  c2:	6841                	lui	x16,0x10
  c4:	8fd5                	or	x15,x15,x13
  c6:	cb1c                	sw	x15,16(x14)
  c8:	200016b7          	lui	x13,0x20001
  cc:	400227b7          	lui	x15,0x40022
  d0:	04000893          	li	x17,64
  d4:	00200337          	lui	x6,0x200
  d8:	4b98                	lw	x14,16(x15)
  da:	01076733          	or	x14,x14,x16
  de:	cb98                	sw	x14,16(x15)
  e0:	47d8                	lw	x14,12(x15)
  e2:	8b05                	andi	x14,x14,1
  e4:	ff75                	bnez	x14,0xe0
  e6:	4702                	lw	x14,0(x2)
  e8:	c23a                	sw	x14,4(x2)
  ea:	c646                	sw	x17,12(x2)
  ec:	4732                	lw	x14,12(x2)
  ee:	ef0d                	bnez	x14,0x128
  f0:	4b98                	lw	x14,16(x15)
  f2:	00676733          	or	x14,x14,x6
  f6:	cb98                	sw	x14,16(x15)
  f8:	47d8                	lw	x14,12(x15)
  fa:	8b05                	andi	x14,x14,1
  fc:	ff75                	bnez	x14,0xf8
  fe:	47d8                	lw	x14,12(x15)
 100:	8b41                	andi	x14,x14,16
 102:	c339                	beqz	x14,0x148
 104:	47d8                	lw	x14,12(x15)
 106:	76c1                	lui	x13,0xffff0
 108:	16fd                	addi	x13,x13,-1 # 0xfffeffff
 10a:	01076713          	ori	x14,x14,16
 10e:	c7d8                	sw	x14,12(x15)
 110:	4b98                	lw	x14,16(x15)
 112:	4521                	li	x10,8
 114:	8f75                	and	x14,x14,x13
 116:	cb98                	sw	x14,16(x15)
 118:	0141                	addi	x2,x2,16
 11a:	9002                	ebreak
 11c:	00d82023          	sw	x13,0(x16) # 0x10000
 120:	b725                	j	0x48
 122:	01032023          	sw	x16,0(x6) # 0x200000
 126:	b7a5                	j	0x8e
 128:	4712                	lw	x14,4(x2)
 12a:	00468e13          	addi	x28,x13,4
 12e:	4294                	lw	x13,0(x13)
 130:	c314                	sw	x13,0(x14)
 132:	4712                	lw	x14,4(x2)
 134:	0711                	addi	x14,x14,4 # 0x40022004
 136:	c23a                	sw	x14,4(x2)
 138:	4732                	lw	x14,12(x2)
 13a:	177d                	addi	x14,x14,-1
 13c:	c63a                	sw	x14,12(x2)
 13e:	47d8                	lw	x14,12(x15)
 140:	8b09                	andi	x14,x14,2
 142:	ff75                	bnez	x14,0x13e
 144:	86f2                	mv	x13,x28
 146:	b75d                	j	0xec
 148:	4702                	lw	x14,0(x2)
 14a:	10070713          	addi	x14,x14,256
 14e:	c03a                	sw	x14,0(x2)
 150:	4722                	lw	x14,8(x2)
 152:	177d                	addi	x14,x14,-1
 154:	c43a                	sw	x14,8(x2)
 156:	f349                	bnez	x14,0xd8
 158:	4b98                	lw	x14,16(x15)
 15a:	76c1                	lui	x13,0xffff0
 15c:	16fd                	addi	x13,x13,-1 # 0xfffeffff
 15e:	8f75                	and	x14,x14,x13
 160:	cb98                	sw	x14,16(x15)
 162:	8941                	andi	x10,x10,16
 164:	c915                	beqz	x10,0x198
 166:	c02e                	sw	x11,0(x2)
 168:	060d                	addi	x12,x12,3
 16a:	c402                	sw	x0,8(x2)
 16c:	8209                	srli	x12,x12,0x2
 16e:	c632                	sw	x12,12(x2)
 170:	200017b7          	lui	x15,0x20001
 174:	4398                	lw	x14,0(x15)
 176:	00478613          	addi	x12,x15,4 # 0x20001004
 17a:	47a2                	lw	x15,8(x2)
 17c:	4682                	lw	x13,0(x2)
 17e:	078a                	slli	x15,x15,0x2
 180:	97b6                	add	x15,x15,x13
 182:	439c                	lw	x15,0(x15)
 184:	00f71c63          	bne	x14,x15,0x19c
 188:	47a2                	lw	x15,8(x2)
 18a:	0785                	addi	x15,x15,1
 18c:	c43e                	sw	x15,8(x2)
 18e:	46a2                	lw	x13,8(x2)
 190:	4732                	lw	x14,12(x2)
 192:	87b2                	mv	x15,x12
 194:	fee6e0e3          	bltu	x13,x14,0x174
 198:	4501                	li	x10,0
 19a:	bfbd                	j	0x118
 19c:	4541                	li	x10,16
 19e:	bfad                	j	0x118
 1a0:	ffff                	.insn	2, 0xffff
 1a2:	ffff                	.insn	2, 0xffff
 1a4:	ffff                	.insn	2, 0xffff
 1a6:	ffff                	.insn	2, 0xffff
 1a8:	ffff                	.insn	2, 0xffff
 1aa:	ffff                	.insn	2, 0xffff
 1ac:	ffff                	.insn	2, 0xffff
 1ae:	ffff                	.insn	2, 0xffff
 1b0:	ffff                	.insn	2, 0xffff
 1b2:	ffff                	.insn	2, 0xffff
 1b4:	ffff                	.insn	2, 0xffff
 1b6:	ffff                	.insn	2, 0xffff
 1b8:	ffff                	.insn	2, 0xffff
 1ba:	ffff                	.insn	2, 0xffff
 1bc:	ffff                	.insn	2, 0xffff
 1be:	ffff                	.insn	2, 0xffff
 1c0:	ffff                	.insn	2, 0xffff
 1c2:	ffff                	.insn	2, 0xffff
 1c4:	ffff                	.insn	2, 0xffff
 1c6:	ffff                	.insn	2, 0xffff
 1c8:	ffff                	.insn	2, 0xffff
 1ca:	ffff                	.insn	2, 0xffff
 1cc:	ffff                	.insn	2, 0xffff
 1ce:	ffff                	.insn	2, 0xffff
 1d0:	ffff                	.insn	2, 0xffff
 1d2:	ffff                	.insn	2, 0xffff
 1d4:	ffff                	.insn	2, 0xffff
 1d6:	ffff                	.insn	2, 0xffff
 1d8:	ffff                	.insn	2, 0xffff
 1da:	ffff                	.insn	2, 0xffff
 1dc:	ffff                	.insn	2, 0xffff
 1de:	ffff                	.insn	2, 0xffff
 1e0:	ffff                	.insn	2, 0xffff
 1e2:	ffff                	.insn	2, 0xffff
 1e4:	ffff                	.insn	2, 0xffff
 1e6:	ffff                	.insn	2, 0xffff
 1e8:	ffff                	.insn	2, 0xffff
 1ea:	ffff                	.insn	2, 0xffff
 1ec:	ffff                	.insn	2, 0xffff
 1ee:	ffff                	.insn	2, 0xffff
 1f0:	ffff                	.insn	2, 0xffff
 1f2:	ffff                	.insn	2, 0xffff
 1f4:	ffff                	.insn	2, 0xffff
 1f6:	ffff                	.insn	2, 0xffff
 1f8:	ffff                	.insn	2, 0xffff
 1fa:	ffff                	.insn	2, 0xffff
 1fc:	ffff                	.insn	2, 0xffff
 1fe:	ffff                	.insn	2, 0xffff
