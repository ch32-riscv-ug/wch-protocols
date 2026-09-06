# b003-write_block-commented  (80 bytes)
# riscv-none-elf-objdump -D -b binary -m riscv:rv32 -M numeric
   0:	05450713          	addi	x14,x10,84
   4:	430c                	lw	x11,0(x14)
   6:	435c                	lw	x15,4(x14)
   8:	00871283          	lh	x5,8(x14)
   c:	00a71303          	lh	x6,10(x14)
  10:	932e                	add	x6,x6,x11
  12:	000106b7          	lui	x13,0x10
  16:	c3d4                	sw	x13,4(x15)
  18:	00558633          	add	x12,x11,x5
  1c:	000906b7          	lui	x13,0x90
  20:	c3d4                	sw	x13,4(x15)
  22:	4194                	lw	x13,0(x11)
  24:	4754                	lw	x13,12(x14)
  26:	c194                	sw	x13,0(x11)
  28:	000506b7          	lui	x13,0x50
  2c:	c3d4                	sw	x13,4(x15)
  2e:	4194                	lw	x13,0(x11)
  30:	0591                	addi	x11,x11,4
  32:	0711                	addi	x14,x14,4
  34:	fec5c8e3          	blt	x11,x12,0x24
  38:	000106b7          	lui	x13,0x10
  3c:	04068693          	addi	x13,x13,64 # 0x10040
  40:	c3d4                	sw	x13,4(x15)
  42:	ffc5a683          	lw	x13,-4(x11)
  46:	fc65c9e3          	blt	x11,x6,0x18
  4a:	56fd                	li	x13,-1
  4c:	c114                	sw	x13,0(x10)
  4e:	8082                	ret
