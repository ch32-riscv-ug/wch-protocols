# b003-write64_flash  (48 bytes)
# riscv-none-elf-objdump -D -b binary -m riscv:rv32 -M numeric
   0:	03450713          	addi	x14,x10,52
   4:	430c                	lw	x11,0(x14)
   6:	04058613          	addi	x12,x11,64
   a:	435c                	lw	x15,4(x14)
   c:	c78c                	sw	x11,8(x15)
   e:	4714                	lw	x13,8(x14)
  10:	c194                	sw	x13,0(x11)
  12:	000506b7          	lui	x13,0x50
  16:	c3d4                	sw	x13,4(x15)
  18:	4194                	lw	x13,0(x11)
  1a:	0591                	addi	x11,x11,4
  1c:	0711                	addi	x14,x14,4
  1e:	fec5c8e3          	blt	x11,x12,0xe
  22:	66c1                	lui	x13,0x10
  24:	04068693          	addi	x13,x13,64 # 0x10040
  28:	c3d4                	sw	x13,4(x15)
  2a:	56fd                	li	x13,-1
  2c:	c114                	sw	x13,0(x10)
  2e:	8082                	ret
