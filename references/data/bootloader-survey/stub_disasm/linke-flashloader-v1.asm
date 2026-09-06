# linke-flashloader-v1  (512 bytes)
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
  28:	c79d                	beqz	x15,0x56
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
  4c:	10071f63          	bnez	x14,0x16a
  50:	4b98                	lw	x14,16(x15)
  52:	9b6d                	andi	x14,x14,-5
  54:	cb98                	sw	x14,16(x15)
  56:	00457793          	andi	x15,x10,4
  5a:	cba9                	beqz	x15,0xac
  5c:	07f60793          	addi	x15,x12,127
  60:	839d                	srli	x15,x15,0x7
  62:	c02e                	sw	x11,0(x2)
  64:	682d                	lui	x16,0xb
  66:	7681                	lui	x13,0xfffe0
  68:	c43e                	sw	x15,8(x2)
  6a:	000208b7          	lui	x17,0x20
  6e:	400227b7          	lui	x15,0x40022
  72:	40003337          	lui	x6,0x40003
  76:	aaa80813          	addi	x16,x16,-1366 # 0xaaaa
  7a:	16fd                	addi	x13,x13,-1 # 0xfffdffff
  7c:	4b98                	lw	x14,16(x15)
  7e:	01176733          	or	x14,x14,x17
  82:	cb98                	sw	x14,16(x15)
  84:	4702                	lw	x14,0(x2)
  86:	cbd8                	sw	x14,20(x15)
  88:	4b98                	lw	x14,16(x15)
  8a:	04076713          	ori	x14,x14,64
  8e:	cb98                	sw	x14,16(x15)
  90:	47d8                	lw	x14,12(x15)
  92:	8b05                	andi	x14,x14,1
  94:	ef71                	bnez	x14,0x170
  96:	4b98                	lw	x14,16(x15)
  98:	8f75                	and	x14,x14,x13
  9a:	cb98                	sw	x14,16(x15)
  9c:	4702                	lw	x14,0(x2)
  9e:	08070713          	addi	x14,x14,128
  a2:	c03a                	sw	x14,0(x2)
  a4:	4722                	lw	x14,8(x2)
  a6:	177d                	addi	x14,x14,-1
  a8:	c43a                	sw	x14,8(x2)
  aa:	fb69                	bnez	x14,0x7c
  ac:	00857793          	andi	x15,x10,8
  b0:	c3ed                	beqz	x15,0x192
  b2:	07f60793          	addi	x15,x12,127
  b6:	c02e                	sw	x11,0(x2)
  b8:	839d                	srli	x15,x15,0x7
  ba:	40022737          	lui	x14,0x40022
  be:	c43e                	sw	x15,8(x2)
  c0:	4b1c                	lw	x15,16(x14)
  c2:	66c1                	lui	x13,0x10
  c4:	00080837          	lui	x16,0x80
  c8:	8fd5                	or	x15,x15,x13
  ca:	cb1c                	sw	x15,16(x14)
  cc:	48a1                	li	x17,8
  ce:	20001737          	lui	x14,0x20001
  d2:	400227b7          	lui	x15,0x40022
  d6:	00040337          	lui	x6,0x40
  da:	4b94                	lw	x13,16(x15)
  dc:	0106e6b3          	or	x13,x13,x16
  e0:	cb94                	sw	x13,16(x15)
  e2:	47d4                	lw	x13,12(x15)
  e4:	8a85                	andi	x13,x13,1
  e6:	fef5                	bnez	x13,0xe2
  e8:	4682                	lw	x13,0(x2)
  ea:	8e3a                	mv	x28,x14
  ec:	c236                	sw	x13,4(x2)
  ee:	c646                	sw	x17,12(x2)
  f0:	4692                	lw	x13,4(x2)
  f2:	00072e83          	lw	x29,0(x14) # 0x20001000
  f6:	0741                	addi	x14,x14,16
  f8:	01d6a023          	sw	x29,0(x13) # 0x10000
  fc:	4692                	lw	x13,4(x2)
  fe:	ff472e83          	lw	x29,-12(x14)
 102:	01d6a223          	sw	x29,4(x13)
 106:	4692                	lw	x13,4(x2)
 108:	ff872e83          	lw	x29,-8(x14)
 10c:	01d6a423          	sw	x29,8(x13)
 110:	4692                	lw	x13,4(x2)
 112:	00ce2e03          	lw	x28,12(x28)
 116:	01c6a623          	sw	x28,12(x13)
 11a:	4b94                	lw	x13,16(x15)
 11c:	0066e6b3          	or	x13,x13,x6
 120:	cb94                	sw	x13,16(x15)
 122:	47d4                	lw	x13,12(x15)
 124:	8a85                	andi	x13,x13,1
 126:	fef5                	bnez	x13,0x122
 128:	4692                	lw	x13,4(x2)
 12a:	8e3a                	mv	x28,x14
 12c:	06c1                	addi	x13,x13,16
 12e:	c236                	sw	x13,4(x2)
 130:	46b2                	lw	x13,12(x2)
 132:	16fd                	addi	x13,x13,-1
 134:	c636                	sw	x13,12(x2)
 136:	fecd                	bnez	x13,0xf0
 138:	4682                	lw	x13,0(x2)
 13a:	cbd4                	sw	x13,20(x15)
 13c:	4b94                	lw	x13,16(x15)
 13e:	0406e693          	ori	x13,x13,64
 142:	cb94                	sw	x13,16(x15)
 144:	47d4                	lw	x13,12(x15)
 146:	8a85                	andi	x13,x13,1
 148:	fef5                	bnez	x13,0x144
 14a:	47d4                	lw	x13,12(x15)
 14c:	8ad1                	andi	x13,x13,20
 14e:	c685                	beqz	x13,0x176
 150:	47d8                	lw	x14,12(x15)
 152:	fff306b7          	lui	x13,0xfff30
 156:	16fd                	addi	x13,x13,-1 # 0xfff2ffff
 158:	01476713          	ori	x14,x14,20
 15c:	c7d8                	sw	x14,12(x15)
 15e:	4b98                	lw	x14,16(x15)
 160:	4521                	li	x10,8
 162:	8f75                	and	x14,x14,x13
 164:	cb98                	sw	x14,16(x15)
 166:	0141                	addi	x2,x2,16
 168:	9002                	ebreak
 16a:	00d82023          	sw	x13,0(x16) # 0x80000
 16e:	bde9                	j	0x48
 170:	01032023          	sw	x16,0(x6) # 0x40000
 174:	bf31                	j	0x90
 176:	4682                	lw	x13,0(x2)
 178:	08068693          	addi	x13,x13,128
 17c:	c036                	sw	x13,0(x2)
 17e:	46a2                	lw	x13,8(x2)
 180:	16fd                	addi	x13,x13,-1
 182:	c436                	sw	x13,8(x2)
 184:	fab9                	bnez	x13,0xda
 186:	4b98                	lw	x14,16(x15)
 188:	fff306b7          	lui	x13,0xfff30
 18c:	16fd                	addi	x13,x13,-1 # 0xfff2ffff
 18e:	8f75                	and	x14,x14,x13
 190:	cb98                	sw	x14,16(x15)
 192:	8941                	andi	x10,x10,16
 194:	c915                	beqz	x10,0x1c8
 196:	c02e                	sw	x11,0(x2)
 198:	060d                	addi	x12,x12,3
 19a:	c402                	sw	x0,8(x2)
 19c:	8209                	srli	x12,x12,0x2
 19e:	c632                	sw	x12,12(x2)
 1a0:	200017b7          	lui	x15,0x20001
 1a4:	4398                	lw	x14,0(x15)
 1a6:	00478613          	addi	x12,x15,4 # 0x20001004
 1aa:	47a2                	lw	x15,8(x2)
 1ac:	4682                	lw	x13,0(x2)
 1ae:	078a                	slli	x15,x15,0x2
 1b0:	97b6                	add	x15,x15,x13
 1b2:	439c                	lw	x15,0(x15)
 1b4:	00f71c63          	bne	x14,x15,0x1cc
 1b8:	47a2                	lw	x15,8(x2)
 1ba:	0785                	addi	x15,x15,1
 1bc:	c43e                	sw	x15,8(x2)
 1be:	46a2                	lw	x13,8(x2)
 1c0:	4732                	lw	x14,12(x2)
 1c2:	87b2                	mv	x15,x12
 1c4:	fee6e0e3          	bltu	x13,x14,0x1a4
 1c8:	4501                	li	x10,0
 1ca:	bf71                	j	0x166
 1cc:	4541                	li	x10,16
 1ce:	bf61                	j	0x166
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
