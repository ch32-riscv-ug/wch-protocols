# b003-half_wise_read  (48 bytes)
# riscv-none-elf-objdump -D -b binary -m riscv:rv32 -M numeric
   0:	0005a023          	sw	x0,0(x11)
   4:	03450713          	addi	x14,x10,52
   8:	430c                	lw	x11,0(x14)
   a:	4350                	lw	x12,4(x14)
   c:	962e                	add	x12,x12,x11
   e:	0721                	addi	x14,x14,8
  10:	2196                	fld	f3,320(x2)
  12:	a316                	fsd	f5,384(x2)
  14:	0589                	addi	x11,x11,2
  16:	0709                	addi	x14,x14,2
  18:	fec5cce3          	blt	x11,x12,0x10
  1c:	fff00693          	li	x13,-1
  20:	c114                	sw	x13,0(x10)
  22:	8082                	ret
	...
