# b003stub-erase_block  (52 bytes)
# riscv-none-elf-objdump -D -b binary -m riscv:rv32 -M numeric
   0:	03850713          	addi	x14,x10,56
   4:	430c                	lw	x11,0(x14)
   6:	435c                	lw	x15,4(x14)
   8:	00871283          	lh	x5,8(x14)
   c:	00a71603          	lh	x12,10(x14)
  10:	962e                	add	x12,x12,x11
  12:	000206b7          	lui	x13,0x20
  16:	c3d4                	sw	x13,4(x15)
  18:	04068693          	addi	x13,x13,64 # 0x20040
  1c:	c78c                	sw	x11,8(x15)
  1e:	c3d4                	sw	x13,4(x15)
  20:	4398                	lw	x14,0(x15)
  22:	8b05                	andi	x14,x14,1
  24:	ff75                	bnez	x14,0x20
  26:	9596                	add	x11,x11,x5
  28:	fec5cae3          	blt	x11,x12,0x1c
  2c:	56fd                	li	x13,-1
  2e:	c114                	sw	x13,0(x10)
  30:	8082                	ret
  32:	0001                	nop
