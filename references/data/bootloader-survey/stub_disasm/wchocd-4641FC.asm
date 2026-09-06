# wchocd-4641FC  (160 bytes)  @0x4641FC  fnv1a64=74fa8c30796c37ba
# source: wch:tools/OpenOCD/OpenOCD/bin/openocd (WCH 配布の Linux OpenOCD, .rodata)
# 既知 blob との一致: (新規)
# riscv-none-elf-objdump -D -b binary -m riscv:rv32 -M numeric
   0:	1101                	addi	x2,x2,-32
   2:	ce22                	sw	x8,28(x2)
   4:	1000                	addi	x8,x2,32
   6:	fea42623          	sw	x10,-20(x8)
   a:	0001                	nop
   c:	0001                	nop
   e:	0001                	nop
  10:	0001                	nop
  12:	0001                	nop
  14:	0001                	nop
  16:	0001                	nop
  18:	0001                	nop
  1a:	0001                	nop
  1c:	0001                	nop
  1e:	0001                	nop
  20:	0001                	nop
  22:	0001                	nop
  24:	0001                	nop
  26:	4472                	lw	x8,28(x2)
  28:	6105                	addi	x2,x2,32
  2a:	8082                	ret
  2c:	1101                	addi	x2,x2,-32
  2e:	ce06                	sw	x1,28(x2)
  30:	cc22                	sw	x8,24(x2)
  32:	1000                	addi	x8,x2,32
  34:	404007b7          	lui	x15,0x40400
  38:	07e9                	addi	x15,x15,26 # 0x4040001a
  3a:	471d                	li	x14,7
  3c:	a398                	fsd	f14,0(x15)
  3e:	4501                	li	x10,0
  40:	37c1                	addiw	x15,x15,-16
  42:	404007b7          	lui	x15,0x40400
  46:	07e1                	addi	x15,x15,24 # 0x40400018
  48:	4715                	li	x14,5
  4a:	a398                	fsd	f14,0(x15)
  4c:	4501                	li	x10,0
  4e:	3f4d                	addiw	x30,x30,-13
  50:	404007b7          	lui	x15,0x40400
  54:	07e1                	addi	x15,x15,24 # 0x40400018
  56:	239c                	fld	f15,0(x15)
  58:	0ff7f793          	zext.b	x15,x15
  5c:	fef407a3          	sb	x15,-17(x8)
  60:	4501                	li	x10,0
  62:	3f79                	addiw	x30,x30,-2
  64:	404007b7          	lui	x15,0x40400
  68:	07e1                	addi	x15,x15,24 # 0x40400018
  6a:	239c                	fld	f15,0(x15)
  6c:	0ff7f793          	zext.b	x15,x15
  70:	fef407a3          	sb	x15,-17(x8)
  74:	4501                	li	x10,0
  76:	3769                	addiw	x14,x14,-6
  78:	404007b7          	lui	x15,0x40400
  7c:	07e9                	addi	x15,x15,26 # 0x4040001a
  7e:	00078023          	sb	x0,0(x15)
  82:	4501                	li	x10,0
  84:	3fb5                	addiw	x31,x31,-19
  86:	fef44783          	lbu	x15,-17(x8)
  8a:	0ff7f793          	zext.b	x15,x15
  8e:	8b85                	andi	x15,x15,1
  90:	c391                	beqz	x15,0x94
  92:	b74d                	j	0x34
  94:	0001                	nop
  96:	0001                	nop
  98:	40f2                	lw	x1,28(x2)
  9a:	4462                	lw	x8,24(x2)
  9c:	6105                	addi	x2,x2,32
  9e:	8082                	ret
