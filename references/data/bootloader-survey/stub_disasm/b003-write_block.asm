# b003-write_block  (104 bytes)
# riscv-none-elf-objdump -D -b binary -m riscv:rv32 -M numeric
   0:	300027f3          	csrr	x15,mstatus
   4:	f777f793          	andi	x15,x15,-137
   8:	30079073          	csrw	mstatus,x15
   c:	06c50713          	addi	x14,x10,108
  10:	430c                	lw	x11,0(x14)
  12:	435c                	lw	x15,4(x14)
  14:	00871283          	lh	x5,8(x14)
  18:	00a71303          	lh	x6,10(x14)
  1c:	932e                	add	x6,x6,x11
  1e:	000106b7          	lui	x13,0x10
  22:	c3d4                	sw	x13,4(x15)
  24:	00558633          	add	x12,x11,x5
  28:	000906b7          	lui	x13,0x90
  2c:	c3d4                	sw	x13,4(x15)
  2e:	4194                	lw	x13,0(x11)
  30:	4754                	lw	x13,12(x14)
  32:	c194                	sw	x13,0(x11)
  34:	000506b7          	lui	x13,0x50
  38:	c3d4                	sw	x13,4(x15)
  3a:	4194                	lw	x13,0(x11)
  3c:	0591                	addi	x11,x11,4
  3e:	0711                	addi	x14,x14,4
  40:	fec5c8e3          	blt	x11,x12,0x30
  44:	000106b7          	lui	x13,0x10
  48:	04068693          	addi	x13,x13,64 # 0x10040
  4c:	c3d4                	sw	x13,4(x15)
  4e:	ffc5a683          	lw	x13,-4(x11)
  52:	fc65c9e3          	blt	x11,x6,0x24
  56:	56fd                	li	x13,-1
  58:	c114                	sw	x13,0(x10)
  5a:	300027f3          	csrr	x15,mstatus
  5e:	0887e793          	ori	x15,x15,136
  62:	30079073          	csrw	mstatus,x15
  66:	8082                	ret
