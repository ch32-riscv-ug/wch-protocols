# wchocd-463CA0  (1372 bytes)  @0x463CA0  fnv1a64=4e66d147e14535f8
# source: wch:tools/OpenOCD/OpenOCD/bin/openocd (WCH 配布の Linux OpenOCD, .rodata)
# 既知 blob との一致: (新規)
# riscv-none-elf-objdump -D -b binary -m riscv:rv32 -M numeric
   0:	7139                	addi	x2,x2,-64
   2:	de06                	sw	x1,60(x2)
   4:	dc22                	sw	x8,56(x2)
   6:	0080                	addi	x8,x2,64
   8:	87aa                	mv	x15,x10
   a:	fcb42423          	sw	x11,-56(x8)
   e:	fcc42223          	sw	x12,-60(x8)
  12:	fcf407a3          	sb	x15,-49(x8)
  16:	fc042c23          	sw	x0,-40(x8)
  1a:	fcf44783          	lbu	x15,-49(x8)
  1e:	8b89                	andi	x15,x15,2
  20:	eb91                	bnez	x15,0x34
  22:	fcf44783          	lbu	x15,-49(x8)
  26:	8ba1                	andi	x15,x15,8
  28:	e791                	bnez	x15,0x34
  2a:	fcf44783          	lbu	x15,-49(x8)
  2e:	8b91                	andi	x15,x15,4
  30:	0e078463          	beqz	x15,0x118
  34:	2b91                	addiw	x23,x23,4
  36:	404007b7          	lui	x15,0x40400
  3a:	07e9                	addi	x15,x15,26 # 0x4040001a
  3c:	470d                	li	x14,3
  3e:	a398                	fsd	f14,0(x15)
  40:	4501                	li	x10,0
  42:	2b29                	addiw	x22,x22,10
  44:	404007b7          	lui	x15,0x40400
  48:	07e1                	addi	x15,x15,24 # 0x40400018
  4a:	4719                	li	x14,6
  4c:	a398                	fsd	f14,0(x15)
  4e:	4501                	li	x10,0
  50:	2331                	addiw	x6,x6,12
  52:	404007b7          	lui	x15,0x40400
  56:	07e9                	addi	x15,x15,26 # 0x4040001a
  58:	00078023          	sb	x0,0(x15)
  5c:	4501                	li	x10,0
  5e:	29fd                	addiw	x19,x19,31
  60:	404007b7          	lui	x15,0x40400
  64:	07e1                	addi	x15,x15,24 # 0x40400018
  66:	04800713          	li	x14,72
  6a:	a398                	fsd	f14,0(x15)
  6c:	4501                	li	x10,0
  6e:	21fd                	addiw	x3,x3,31
  70:	404007b7          	lui	x15,0x40400
  74:	07e1                	addi	x15,x15,24 # 0x40400018
  76:	00078023          	sb	x0,0(x15)
  7a:	4501                	li	x10,0
  7c:	21c5                	addiw	x3,x3,17
  7e:	404007b7          	lui	x15,0x40400
  82:	07e1                	addi	x15,x15,24 # 0x40400018
  84:	03000713          	li	x14,48
  88:	a398                	fsd	f14,0(x15)
  8a:	4501                	li	x10,0
  8c:	29c1                	addiw	x19,x19,16
  8e:	404007b7          	lui	x15,0x40400
  92:	07e1                	addi	x15,x15,24 # 0x40400018
  94:	471d                	li	x14,7
  96:	a398                	fsd	f14,0(x15)
  98:	4501                	li	x10,0
  9a:	21c9                	addiw	x3,x3,18
  9c:	404007b7          	lui	x15,0x40400
  a0:	07e1                	addi	x15,x15,24 # 0x40400018
  a2:	239c                	fld	f15,0(x15)
  a4:	0ff7f793          	zext.b	x15,x15
  a8:	fef42223          	sw	x15,-28(x8)
  ac:	4501                	li	x10,0
  ae:	217d                	addiw	x2,x2,31
  b0:	404007b7          	lui	x15,0x40400
  b4:	07e1                	addi	x15,x15,24 # 0x40400018
  b6:	239c                	fld	f15,0(x15)
  b8:	0ff7f793          	zext.b	x15,x15
  bc:	fef42223          	sw	x15,-28(x8)
  c0:	4501                	li	x10,0
  c2:	2969                	addiw	x18,x18,26
  c4:	404007b7          	lui	x15,0x40400
  c8:	07e1                	addi	x15,x15,24 # 0x40400018
  ca:	239c                	fld	f15,0(x15)
  cc:	0ff7f793          	zext.b	x15,x15
  d0:	fcf40ba3          	sb	x15,-41(x8)
  d4:	4501                	li	x10,0
  d6:	2159                	addiw	x2,x2,22
  d8:	404007b7          	lui	x15,0x40400
  dc:	07e9                	addi	x15,x15,26 # 0x4040001a
  de:	00078023          	sb	x0,0(x15)
  e2:	fd744783          	lbu	x15,-41(x8)
  e6:	0ff7f793          	zext.b	x15,x15
  ea:	0207f793          	andi	x15,x15,32
  ee:	cb99                	beqz	x15,0x104
  f0:	fc842783          	lw	x15,-56(x8)
  f4:	0ff7f713          	zext.b	x14,x15
  f8:	000707b7          	lui	x15,0x70
  fc:	00f76e63          	bltu	x14,x15,0x118
 100:	47c1                	li	x15,16
 102:	a981                	j	0x552
 104:	fc842783          	lw	x15,-56(x8)
 108:	0ff7f713          	zext.b	x14,x15
 10c:	000307b7          	lui	x15,0x30
 110:	00f76463          	bltu	x14,x15,0x118
 114:	47c1                	li	x15,16
 116:	a935                	j	0x552
 118:	fcf44783          	lbu	x15,-49(x8)
 11c:	8b89                	andi	x15,x15,2
 11e:	0c078d63          	beqz	x15,0x1f8
 122:	fc842783          	lw	x15,-56(x8)
 126:	fef42423          	sw	x15,-24(x8)
 12a:	fd744783          	lbu	x15,-41(x8)
 12e:	0ff7f793          	zext.b	x15,x15
 132:	0207f793          	andi	x15,x15,32
 136:	c789                	beqz	x15,0x140
 138:	479d                	li	x15,7
 13a:	fef42023          	sw	x15,-32(x8)
 13e:	a021                	j	0x146
 140:	478d                	li	x15,3
 142:	fef42023          	sw	x15,-32(x8)
 146:	2189                	addiw	x3,x3,2
 148:	404007b7          	lui	x15,0x40400
 14c:	07e9                	addi	x15,x15,26 # 0x4040001a
 14e:	470d                	li	x14,3
 150:	a398                	fsd	f14,0(x15)
 152:	4501                	li	x10,0
 154:	2121                	addiw	x2,x2,8
 156:	404007b7          	lui	x15,0x40400
 15a:	07e1                	addi	x15,x15,24 # 0x40400018
 15c:	4719                	li	x14,6
 15e:	a398                	fsd	f14,0(x15)
 160:	4501                	li	x10,0
 162:	2eed                	addiw	x29,x29,27
 164:	404007b7          	lui	x15,0x40400
 168:	07e9                	addi	x15,x15,26 # 0x4040001a
 16a:	00078023          	sb	x0,0(x15)
 16e:	4501                	li	x10,0
 170:	26f5                	addiw	x13,x13,29
 172:	404007b7          	lui	x15,0x40400
 176:	07e9                	addi	x15,x15,26 # 0x4040001a
 178:	470d                	li	x14,3
 17a:	a398                	fsd	f14,0(x15)
 17c:	4501                	li	x10,0
 17e:	2ef9                	addiw	x29,x29,30
 180:	404007b7          	lui	x15,0x40400
 184:	07e1                	addi	x15,x15,24 # 0x40400018
 186:	fd800713          	li	x14,-40
 18a:	a398                	fsd	f14,0(x15)
 18c:	4501                	li	x10,0
 18e:	26f9                	addiw	x13,x13,30
 190:	fe842783          	lw	x15,-24(x8)
 194:	0107d713          	srli	x14,x15,0x10
 198:	404007b7          	lui	x15,0x40400
 19c:	07e1                	addi	x15,x15,24 # 0x40400018
 19e:	0ff77713          	zext.b	x14,x14
 1a2:	a398                	fsd	f14,0(x15)
 1a4:	4501                	li	x10,0
 1a6:	2e5d                	addiw	x28,x28,23
 1a8:	fe842783          	lw	x15,-24(x8)
 1ac:	0087d713          	srli	x14,x15,0x8
 1b0:	404007b7          	lui	x15,0x40400
 1b4:	07e1                	addi	x15,x15,24 # 0x40400018
 1b6:	0ff77713          	zext.b	x14,x14
 1ba:	a398                	fsd	f14,0(x15)
 1bc:	4501                	li	x10,0
 1be:	2e79                	addiw	x28,x28,30
 1c0:	fe842703          	lw	x14,-24(x8)
 1c4:	404007b7          	lui	x15,0x40400
 1c8:	07e1                	addi	x15,x15,24 # 0x40400018
 1ca:	0ff77713          	zext.b	x14,x14
 1ce:	a398                	fsd	f14,0(x15)
 1d0:	4501                	li	x10,0
 1d2:	2669                	addiw	x12,x12,26
 1d4:	404007b7          	lui	x15,0x40400
 1d8:	07e9                	addi	x15,x15,26 # 0x4040001a
 1da:	00078023          	sb	x0,0(x15)
 1de:	266d                	addiw	x12,x12,27
 1e0:	fe842703          	lw	x14,-24(x8)
 1e4:	67c1                	lui	x15,0x10
 1e6:	97ba                	add	x15,x15,x14
 1e8:	fef42423          	sw	x15,-24(x8)
 1ec:	fe042783          	lw	x15,-32(x8)
 1f0:	17fd                	addi	x15,x15,-1 # 0xffff
 1f2:	fef42023          	sw	x15,-32(x8)
 1f6:	fba9                	bnez	x15,0x148
 1f8:	fcf44783          	lbu	x15,-49(x8)
 1fc:	8b91                	andi	x15,x15,4
 1fe:	0c078763          	beqz	x15,0x2cc
 202:	fc842783          	lw	x15,-56(x8)
 206:	fef42423          	sw	x15,-24(x8)
 20a:	fc442703          	lw	x14,-60(x8)
 20e:	6785                	lui	x15,0x1
 210:	17fd                	addi	x15,x15,-1 # 0xfff
 212:	97ba                	add	x15,x15,x14
 214:	83b1                	srli	x15,x15,0xc
 216:	fef42023          	sw	x15,-32(x8)
 21a:	26bd                	addiw	x13,x13,15
 21c:	404007b7          	lui	x15,0x40400
 220:	07e9                	addi	x15,x15,26 # 0x4040001a
 222:	470d                	li	x14,3
 224:	a398                	fsd	f14,0(x15)
 226:	4501                	li	x10,0
 228:	2e15                	addiw	x28,x28,5
 22a:	404007b7          	lui	x15,0x40400
 22e:	07e1                	addi	x15,x15,24 # 0x40400018
 230:	4719                	li	x14,6
 232:	a398                	fsd	f14,0(x15)
 234:	4501                	li	x10,0
 236:	261d                	addiw	x12,x12,7
 238:	404007b7          	lui	x15,0x40400
 23c:	07e9                	addi	x15,x15,26 # 0x4040001a
 23e:	00078023          	sb	x0,0(x15)
 242:	4501                	li	x10,0
 244:	2e21                	addiw	x28,x28,8
 246:	404007b7          	lui	x15,0x40400
 24a:	07e9                	addi	x15,x15,26 # 0x4040001a
 24c:	470d                	li	x14,3
 24e:	a398                	fsd	f14,0(x15)
 250:	4501                	li	x10,0
 252:	2629                	addiw	x12,x12,10
 254:	404007b7          	lui	x15,0x40400
 258:	07e1                	addi	x15,x15,24 # 0x40400018
 25a:	02000713          	li	x14,32
 25e:	a398                	fsd	f14,0(x15)
 260:	4501                	li	x10,0
 262:	2ced                	addiw	x25,x25,27
 264:	fe842783          	lw	x15,-24(x8)
 268:	0107d713          	srli	x14,x15,0x10
 26c:	404007b7          	lui	x15,0x40400
 270:	07e1                	addi	x15,x15,24 # 0x40400018
 272:	0ff77713          	zext.b	x14,x14
 276:	a398                	fsd	f14,0(x15)
 278:	4501                	li	x10,0
 27a:	24cd                	addiw	x9,x9,19
 27c:	fe842783          	lw	x15,-24(x8)
 280:	0087d713          	srli	x14,x15,0x8
 284:	404007b7          	lui	x15,0x40400
 288:	07e1                	addi	x15,x15,24 # 0x40400018
 28a:	0ff77713          	zext.b	x14,x14
 28e:	a398                	fsd	f14,0(x15)
 290:	4501                	li	x10,0
 292:	24e9                	addiw	x9,x9,26
 294:	fe842703          	lw	x14,-24(x8)
 298:	404007b7          	lui	x15,0x40400
 29c:	07e1                	addi	x15,x15,24 # 0x40400018
 29e:	0ff77713          	zext.b	x14,x14
 2a2:	a398                	fsd	f14,0(x15)
 2a4:	4501                	li	x10,0
 2a6:	2c5d                	addiw	x24,x24,23
 2a8:	404007b7          	lui	x15,0x40400
 2ac:	07e9                	addi	x15,x15,26 # 0x4040001a
 2ae:	00078023          	sb	x0,0(x15)
 2b2:	2cd9                	addiw	x25,x25,22
 2b4:	fe842703          	lw	x14,-24(x8)
 2b8:	6785                	lui	x15,0x1
 2ba:	97ba                	add	x15,x15,x14
 2bc:	fef42423          	sw	x15,-24(x8)
 2c0:	fe042783          	lw	x15,-32(x8)
 2c4:	17fd                	addi	x15,x15,-1 # 0xfff
 2c6:	fef42023          	sw	x15,-32(x8)
 2ca:	fba9                	bnez	x15,0x21c
 2cc:	fcf44783          	lbu	x15,-49(x8)
 2d0:	8ba1                	andi	x15,x15,8
 2d2:	10078663          	beqz	x15,0x3de
 2d6:	fc842783          	lw	x15,-56(x8)
 2da:	fef42423          	sw	x15,-24(x8)
 2de:	200017b7          	lui	x15,0x20001
 2e2:	fef42623          	sw	x15,-20(x8)
 2e6:	fc442783          	lw	x15,-60(x8)
 2ea:	0ff78793          	addi	x15,x15,255 # 0x200010ff
 2ee:	83a1                	srli	x15,x15,0x8
 2f0:	fef42023          	sw	x15,-32(x8)
 2f4:	2c51                	addiw	x24,x24,20
 2f6:	404007b7          	lui	x15,0x40400
 2fa:	07e9                	addi	x15,x15,26 # 0x4040001a
 2fc:	470d                	li	x14,3
 2fe:	a398                	fsd	f14,0(x15)
 300:	4501                	li	x10,0
 302:	2ca9                	addiw	x25,x25,10
 304:	404007b7          	lui	x15,0x40400
 308:	07e1                	addi	x15,x15,24 # 0x40400018
 30a:	4719                	li	x14,6
 30c:	a398                	fsd	f14,0(x15)
 30e:	4501                	li	x10,0
 310:	24b1                	addiw	x9,x9,12
 312:	404007b7          	lui	x15,0x40400
 316:	07e9                	addi	x15,x15,26 # 0x4040001a
 318:	00078023          	sb	x0,0(x15)
 31c:	4501                	li	x10,0
 31e:	2c3d                	addiw	x24,x24,15
 320:	404007b7          	lui	x15,0x40400
 324:	07e9                	addi	x15,x15,26 # 0x4040001a
 326:	470d                	li	x14,3
 328:	a398                	fsd	f14,0(x15)
 32a:	4501                	li	x10,0
 32c:	2c05                	addiw	x24,x24,1
 32e:	404007b7          	lui	x15,0x40400
 332:	07e1                	addi	x15,x15,24 # 0x40400018
 334:	4709                	li	x14,2
 336:	a398                	fsd	f14,0(x15)
 338:	4501                	li	x10,0
 33a:	240d                	addiw	x8,x8,3
 33c:	fe842783          	lw	x15,-24(x8)
 340:	0107d713          	srli	x14,x15,0x10
 344:	404007b7          	lui	x15,0x40400
 348:	07e1                	addi	x15,x15,24 # 0x40400018
 34a:	0ff77713          	zext.b	x14,x14
 34e:	a398                	fsd	f14,0(x15)
 350:	4501                	li	x10,0
 352:	2429                	addiw	x8,x8,10
 354:	fe842783          	lw	x15,-24(x8)
 358:	0087d713          	srli	x14,x15,0x8
 35c:	404007b7          	lui	x15,0x40400
 360:	07e1                	addi	x15,x15,24 # 0x40400018
 362:	0ff77713          	zext.b	x14,x14
 366:	a398                	fsd	f14,0(x15)
 368:	4501                	li	x10,0
 36a:	2acd                	addiw	x21,x21,19
 36c:	fe842703          	lw	x14,-24(x8)
 370:	404007b7          	lui	x15,0x40400
 374:	07e1                	addi	x15,x15,24 # 0x40400018
 376:	0ff77713          	zext.b	x14,x14
 37a:	a398                	fsd	f14,0(x15)
 37c:	4501                	li	x10,0
 37e:	2af9                	addiw	x21,x21,30
 380:	10000793          	li	x15,256
 384:	fcf42e23          	sw	x15,-36(x8)
 388:	a02d                	j	0x3b2
 38a:	fec42783          	lw	x15,-20(x8)
 38e:	00178713          	addi	x14,x15,1
 392:	fee42623          	sw	x14,-20(x8)
 396:	40400737          	lui	x14,0x40400
 39a:	0761                	addi	x14,x14,24 # 0x40400018
 39c:	239c                	fld	f15,0(x15)
 39e:	0ff7f793          	zext.b	x15,x15
 3a2:	a31c                	fsd	f15,0(x14)
 3a4:	4501                	li	x10,0
 3a6:	2a5d                	addiw	x20,x20,23
 3a8:	fdc42783          	lw	x15,-36(x8)
 3ac:	17fd                	addi	x15,x15,-1
 3ae:	fcf42e23          	sw	x15,-36(x8)
 3b2:	fdc42783          	lw	x15,-36(x8)
 3b6:	fbf1                	bnez	x15,0x38a
 3b8:	404007b7          	lui	x15,0x40400
 3bc:	07e9                	addi	x15,x15,26 # 0x4040001a
 3be:	00078023          	sb	x0,0(x15)
 3c2:	22d9                	addiw	x5,x5,22
 3c4:	fe842783          	lw	x15,-24(x8)
 3c8:	10078793          	addi	x15,x15,256
 3cc:	fef42423          	sw	x15,-24(x8)
 3d0:	fe042783          	lw	x15,-32(x8)
 3d4:	17fd                	addi	x15,x15,-1
 3d6:	fef42023          	sw	x15,-32(x8)
 3da:	f0079ee3          	bnez	x15,0x2f6
 3de:	fcf44783          	lbu	x15,-49(x8)
 3e2:	8bc1                	andi	x15,x15,16
 3e4:	16078663          	beqz	x15,0x550
 3e8:	fc842783          	lw	x15,-56(x8)
 3ec:	fef42423          	sw	x15,-24(x8)
 3f0:	200017b7          	lui	x15,0x20001
 3f4:	fef42623          	sw	x15,-20(x8)
 3f8:	fe042023          	sw	x0,-32(x8)
 3fc:	fc442783          	lw	x15,-60(x8)
 400:	fcf42e23          	sw	x15,-36(x8)
 404:	2251                	addiw	x4,x4,20 # 0x14
 406:	404007b7          	lui	x15,0x40400
 40a:	07e9                	addi	x15,x15,26 # 0x4040001a
 40c:	471d                	li	x14,7
 40e:	a398                	fsd	f14,0(x15)
 410:	4501                	li	x10,0
 412:	22a9                	addiw	x5,x5,10
 414:	404007b7          	lui	x15,0x40400
 418:	07e1                	addi	x15,x15,24 # 0x40400018
 41a:	472d                	li	x14,11
 41c:	a398                	fsd	f14,0(x15)
 41e:	4501                	li	x10,0
 420:	2a35                	addiw	x20,x20,13
 422:	fe842783          	lw	x15,-24(x8)
 426:	0107d713          	srli	x14,x15,0x10
 42a:	404007b7          	lui	x15,0x40400
 42e:	07e1                	addi	x15,x15,24 # 0x40400018
 430:	0ff77713          	zext.b	x14,x14
 434:	a398                	fsd	f14,0(x15)
 436:	4501                	li	x10,0
 438:	2215                	addiw	x4,x4,5 # 0x5
 43a:	fe842783          	lw	x15,-24(x8)
 43e:	0087d713          	srli	x14,x15,0x8
 442:	404007b7          	lui	x15,0x40400
 446:	07e1                	addi	x15,x15,24 # 0x40400018
 448:	0ff77713          	zext.b	x14,x14
 44c:	a398                	fsd	f14,0(x15)
 44e:	4501                	li	x10,0
 450:	2231                	addiw	x4,x4,12 # 0xc
 452:	fe842703          	lw	x14,-24(x8)
 456:	404007b7          	lui	x15,0x40400
 45a:	07e1                	addi	x15,x15,24 # 0x40400018
 45c:	0ff77713          	zext.b	x14,x14
 460:	a398                	fsd	f14,0(x15)
 462:	4501                	li	x10,0
 464:	28e5                	addiw	x17,x17,25
 466:	404007b7          	lui	x15,0x40400
 46a:	07e1                	addi	x15,x15,24 # 0x40400018
 46c:	239c                	fld	f15,0(x15)
 46e:	0ff7f793          	zext.b	x15,x15
 472:	fef42223          	sw	x15,-28(x8)
 476:	4501                	li	x10,0
 478:	20d5                	addiw	x1,x1,21
 47a:	404007b7          	lui	x15,0x40400
 47e:	07e1                	addi	x15,x15,24 # 0x40400018
 480:	239c                	fld	f15,0(x15)
 482:	0ff7f793          	zext.b	x15,x15
 486:	fef42223          	sw	x15,-28(x8)
 48a:	4501                	li	x10,0
 48c:	28c1                	addiw	x17,x17,16
 48e:	fe042223          	sw	x0,-28(x8)
 492:	fec42783          	lw	x15,-20(x8)
 496:	239c                	fld	f15,0(x15)
 498:	0ff7f793          	zext.b	x15,x15
 49c:	fcf40ba3          	sb	x15,-41(x8)
 4a0:	fec42783          	lw	x15,-20(x8)
 4a4:	00178713          	addi	x14,x15,1
 4a8:	fee42623          	sw	x14,-20(x8)
 4ac:	239c                	fld	f15,0(x15)
 4ae:	0ff7f713          	zext.b	x14,x15
 4b2:	404007b7          	lui	x15,0x40400
 4b6:	07e1                	addi	x15,x15,24 # 0x40400018
 4b8:	239c                	fld	f15,0(x15)
 4ba:	0ff7f793          	zext.b	x15,x15
 4be:	00f70b63          	beq	x14,x15,0x4d4
 4c2:	404007b7          	lui	x15,0x40400
 4c6:	07e9                	addi	x15,x15,26 # 0x4040001a
 4c8:	00078023          	sb	x0,0(x15)
 4cc:	4501                	li	x10,0
 4ce:	2079                	.insn	2, 0x2079
 4d0:	47c1                	li	x15,16
 4d2:	a041                	j	0x552
 4d4:	fd744783          	lbu	x15,-41(x8)
 4d8:	0ff7f793          	zext.b	x15,x15
 4dc:	873e                	mv	x14,x15
 4de:	fe042783          	lw	x15,-32(x8)
 4e2:	078e                	slli	x15,x15,0x3
 4e4:	8be1                	andi	x15,x15,24
 4e6:	00f71733          	sll	x14,x14,x15
 4ea:	fe442783          	lw	x15,-28(x8)
 4ee:	8fd9                	or	x15,x15,x14
 4f0:	fef42223          	sw	x15,-28(x8)
 4f4:	fe042783          	lw	x15,-32(x8)
 4f8:	0037f713          	andi	x14,x15,3
 4fc:	478d                	li	x15,3
 4fe:	00f71b63          	bne	x14,x15,0x514
 502:	fe442703          	lw	x14,-28(x8)
 506:	fd842783          	lw	x15,-40(x8)
 50a:	97ba                	add	x15,x15,x14
 50c:	fcf42c23          	sw	x15,-40(x8)
 510:	fe042223          	sw	x0,-28(x8)
 514:	4501                	li	x10,0
 516:	2099                	addiw	x1,x1,6
 518:	fe042783          	lw	x15,-32(x8)
 51c:	0785                	addi	x15,x15,1
 51e:	fef42023          	sw	x15,-32(x8)
 522:	fe042703          	lw	x14,-32(x8)
 526:	fdc42783          	lw	x15,-36(x8)
 52a:	f6f764e3          	bltu	x14,x15,0x492
 52e:	200027b7          	lui	x15,0x20002
 532:	07c1                	addi	x15,x15,16 # 0x20002010
 534:	4398                	lw	x14,0(x15)
 536:	fd842783          	lw	x15,-40(x8)
 53a:	00f70463          	beq	x14,x15,0x542
 53e:	47c1                	li	x15,16
 540:	a809                	j	0x552
 542:	404007b7          	lui	x15,0x40400
 546:	07e9                	addi	x15,x15,26 # 0x4040001a
 548:	00078023          	sb	x0,0(x15)
 54c:	4501                	li	x10,0
 54e:	2039                	.insn	2, 0x2039
 550:	4781                	li	x15,0
 552:	853e                	mv	x10,x15
 554:	50f2                	lw	x1,60(x2)
 556:	5462                	lw	x8,56(x2)
 558:	6121                	addi	x2,x2,64
 55a:	9002                	ebreak
