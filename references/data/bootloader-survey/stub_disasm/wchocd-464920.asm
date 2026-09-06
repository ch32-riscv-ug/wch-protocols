# wchocd-464920  (504 bytes)  @0x464920  fnv1a64=d3eb8ae426494370
# source: wch:tools/OpenOCD/OpenOCD/bin/openocd (WCH 配布の Linux OpenOCD, .rodata)
# 既知 blob との一致: (新規)
# riscv-none-elf-objdump -D -b binary -m riscv:rv32 -M numeric
   0:	7179                	addi	x2,x2,-48
   2:	d422                	sw	x8,40(x2)
   4:	d04a                	sw	x18,32(x2)
   6:	ca56                	sw	x21,20(x2)
   8:	d606                	sw	x1,44(x2)
   a:	d226                	sw	x9,36(x2)
   c:	ce4e                	sw	x19,28(x2)
   e:	cc52                	sw	x20,24(x2)
  10:	c85a                	sw	x22,16(x2)
  12:	c65e                	sw	x23,12(x2)
  14:	c462                	sw	x24,8(x2)
  16:	00157793          	andi	x15,x10,1
  1a:	842a                	mv	x8,x10
  1c:	8aae                	mv	x21,x11
  1e:	8932                	mv	x18,x12
  20:	e3c1                	bnez	x15,0xa0
  22:	00247793          	andi	x15,x8,2
  26:	cb99                	beqz	x15,0x3c
  28:	0003c6b7          	lui	x13,0x3c
  2c:	4601                	li	x12,0
  2e:	4581                	li	x11,0
  30:	4505                	li	x10,1
  32:	22d9                	addiw	x5,x5,22
  34:	0ff57793          	zext.b	x15,x10
  38:	4509                	li	x10,2
  3a:	efa5                	bnez	x15,0xb2
  3c:	00447793          	andi	x15,x8,4
  40:	cb91                	beqz	x15,0x54
  42:	6685                	lui	x13,0x1
  44:	4601                	li	x12,0
  46:	85d6                	mv	x11,x21
  48:	4505                	li	x10,1
  4a:	227d                	addiw	x4,x4,31 # 0x1f
  4c:	0ff57793          	zext.b	x15,x10
  50:	4511                	li	x10,4
  52:	e3a5                	bnez	x15,0xb2
  54:	01847793          	andi	x15,x8,24
  58:	4a01                	li	x20,0
  5a:	cfd9                	beqz	x15,0xf8
  5c:	200014b7          	lui	x9,0x20001
  60:	0ff90913          	addi	x18,x18,255
  64:	10048493          	addi	x9,x9,256 # 0x20001100
  68:	00895913          	srli	x18,x18,0x8
  6c:	4a01                	li	x20,0
  6e:	409a8ab3          	sub	x21,x21,x9
  72:	00847b93          	andi	x23,x8,8
  76:	01047c13          	andi	x24,x8,16
  7a:	009a8b33          	add	x22,x21,x9
  7e:	040b9663          	bnez	x23,0xca
  82:	060c0663          	beqz	x24,0xee
  86:	f0048993          	addi	x19,x9,-256
  8a:	10000693          	li	x13,256
  8e:	864e                	mv	x12,x19
  90:	85da                	mv	x11,x22
  92:	450d                	li	x10,3
  94:	2295                	addiw	x5,x5,5
  96:	0ff57513          	zext.b	x10,x10
  9a:	c521                	beqz	x10,0xe2
  9c:	4541                	li	x10,16
  9e:	a811                	j	0xb2
  a0:	4681                	li	x13,0
  a2:	4601                	li	x12,0
  a4:	4581                	li	x11,0
  a6:	4521                	li	x10,8
  a8:	2a81                	sext.w	x21,x21
  aa:	0ff57793          	zext.b	x15,x10
  ae:	4505                	li	x10,1
  b0:	dbad                	beqz	x15,0x22
  b2:	50b2                	lw	x1,44(x2)
  b4:	5422                	lw	x8,40(x2)
  b6:	5492                	lw	x9,36(x2)
  b8:	5902                	lw	x18,32(x2)
  ba:	49f2                	lw	x19,28(x2)
  bc:	4a62                	lw	x20,24(x2)
  be:	4ad2                	lw	x21,20(x2)
  c0:	4b42                	lw	x22,16(x2)
  c2:	4bb2                	lw	x23,12(x2)
  c4:	4c22                	lw	x24,8(x2)
  c6:	6145                	addi	x2,x2,48
  c8:	9002                	ebreak
  ca:	10000693          	li	x13,256
  ce:	f0048613          	addi	x12,x9,-256
  d2:	85da                	mv	x11,x22
  d4:	4509                	li	x10,2
  d6:	220d                	addiw	x4,x4,3 # 0x3
  d8:	0ff57513          	zext.b	x10,x10
  dc:	d15d                	beqz	x10,0x82
  de:	4521                	li	x10,8
  e0:	bfc9                	j	0xb2
  e2:	0009a783          	lw	x15,0(x19)
  e6:	0991                	addi	x19,x19,4
  e8:	9a3e                	add	x20,x20,x15
  ea:	ff349ce3          	bne	x9,x19,0xe2
  ee:	197d                	addi	x18,x18,-1
  f0:	10048493          	addi	x9,x9,256
  f4:	f80913e3          	bnez	x18,0x7a
  f8:	8841                	andi	x8,x8,16
  fa:	4501                	li	x10,0
  fc:	d85d                	beqz	x8,0xb2
  fe:	200027b7          	lui	x15,0x20002
 102:	4b9c                	lw	x15,16(x15)
 104:	fb4787e3          	beq	x15,x20,0xb2
 108:	bf51                	j	0x9c
 10a:	80040323          	sb	x0,-2042(x8)
 10e:	4715                	li	x14,5
 110:	87a2                	mv	x15,x8
 112:	80e40323          	sb	x14,-2042(x8)
 116:	0001                	nop
 118:	80a40223          	sb	x10,-2044(x8)
 11c:	8067c703          	lbu	x14,-2042(x15) # 0x20001806
 120:	0762                	slli	x14,x14,0x18
 122:	8761                	srai	x14,x14,0x18
 124:	fe074ce3          	bltz	x14,0x11c
 128:	80a78223          	sb	x10,-2044(x15)
 12c:	8722                	mv	x14,x8
 12e:	80674783          	lbu	x15,-2042(x14)
 132:	07e2                	slli	x15,x15,0x18
 134:	87e1                	srai	x15,x15,0x18
 136:	fe07cce3          	bltz	x15,0x12e
 13a:	8082                	ret
 13c:	80040323          	sb	x0,-2042(x8)
 140:	4715                	li	x14,5
 142:	80e40323          	sb	x14,-2042(x8)
 146:	0001                	nop
 148:	80a40223          	sb	x10,-2044(x8)
 14c:	8082                	ret
 14e:	80640783          	lb	x15,-2042(x8)
 152:	fe07cee3          	bltz	x15,0x14e
 156:	80040323          	sb	x0,-2042(x8)
 15a:	8082                	ret
 15c:	80640783          	lb	x15,-2042(x8)
 160:	fe07cee3          	bltz	x15,0x15c
 164:	80444503          	lbu	x10,-2044(x8)
 168:	8082                	ret
 16a:	80640783          	lb	x15,-2042(x8)
 16e:	fe07cee3          	bltz	x15,0x16a
 172:	80a40223          	sb	x10,-2044(x8)
 176:	8082                	ret
 178:	1141                	addi	x2,x2,-16
 17a:	c426                	sw	x9,8(x2)
 17c:	c24a                	sw	x18,4(x2)
 17e:	c04e                	sw	x19,0(x2)
 180:	c606                	sw	x1,12(x2)
 182:	0bf57713          	andi	x14,x10,191
 186:	47ad                	li	x15,11
 188:	89aa                	mv	x19,x10
 18a:	892e                	mv	x18,x11
 18c:	4495                	li	x9,5
 18e:	00f70663          	beq	x14,x15,0x19a
 192:	4519                	li	x10,6
 194:	3765                	addiw	x14,x14,-7
 196:	3f65                	addiw	x30,x30,-7
 198:	448d                	li	x9,3
 19a:	854e                	mv	x10,x19
 19c:	3745                	addiw	x14,x14,-15
 19e:	59fd                	li	x19,-1
 1a0:	14fd                	addi	x9,x9,-1
 1a2:	01349863          	bne	x9,x19,0x1b2
 1a6:	40b2                	lw	x1,12(x2)
 1a8:	44a2                	lw	x9,8(x2)
 1aa:	4912                	lw	x18,4(x2)
 1ac:	4982                	lw	x19,0(x2)
 1ae:	0141                	addi	x2,x2,16
 1b0:	8082                	ret
 1b2:	01095513          	srli	x10,x18,0x10
 1b6:	0ff57513          	zext.b	x10,x10
 1ba:	3f45                	addiw	x30,x30,-15
 1bc:	0922                	slli	x18,x18,0x8
 1be:	b7cd                	j	0x1a0
 1c0:	1101                	addi	x2,x2,-32
 1c2:	cc26                	sw	x9,24(x2)
 1c4:	ce06                	sw	x1,28(x2)
 1c6:	000804b7          	lui	x9,0x80
 1ca:	3751                	addiw	x14,x14,-12
 1cc:	4515                	li	x10,5
 1ce:	37bd                	addiw	x15,x15,-17
 1d0:	3771                	addiw	x14,x14,-4
 1d2:	3769                	addiw	x14,x14,-6
 1d4:	c62a                	sw	x10,12(x2)
 1d6:	3fa5                	addiw	x31,x31,-23
 1d8:	4532                	lw	x10,12(x2)
 1da:	00157793          	andi	x15,x10,1
 1de:	eb89                	bnez	x15,0x1f0
 1e0:	00156513          	ori	x10,x10,1
 1e4:	0ff57513          	zext.b	x10,x10
 1e8:	40f2                	lw	x1,28(x2)
 1ea:	44e2                	lw	x9,24(x2)
 1ec:	6105                	addi	x2,x2,32
 1ee:	8082                	ret
 1f0:	14fd                	addi	x9,x9,-1 # 0x7ffff
 1f2:	fce9                	bnez	x9,0x1cc
 1f4:	4501                	li	x10,0
 1f6:	bfcd                	j	0x1e8
