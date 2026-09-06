# b003-run_app  (120 bytes)
# riscv-none-elf-objdump -D -b binary -m riscv:rv32 -M numeric
   0:	1ffff5b7          	lui	x11,0x1ffff
   4:	77c58793          	addi	x15,x11,1916 # 0x1ffff77c
   8:	0007a703          	lw	x14,0(x15)
   c:	01075713          	srli	x14,x14,0x10
  10:	00079683          	lh	x13,0(x15)
  14:	77c6c793          	xori	x15,x13,1916
  18:	00f71663          	bne	x14,x15,0x24
  1c:	00b68733          	add	x14,x13,x11
  20:	00070067          	jr	x14
  24:	400227b7          	lui	x15,0x40022
  28:	02878793          	addi	x15,x15,40 # 0x40022028
  2c:	45670737          	lui	x14,0x45670
  30:	12370713          	addi	x14,x14,291 # 0x45670123
  34:	00e7a023          	sw	x14,0(x15)
  38:	400227b7          	lui	x15,0x40022
  3c:	02878793          	addi	x15,x15,40 # 0x40022028
  40:	cdef9737          	lui	x14,0xcdef9
  44:	9ab70713          	addi	x14,x14,-1621 # 0xcdef89ab
  48:	00e7a023          	sw	x14,0(x15)
  4c:	400227b7          	lui	x15,0x40022
  50:	00c78793          	addi	x15,x15,12 # 0x4002200c
  54:	0007a023          	sw	x0,0(x15)
  58:	400227b7          	lui	x15,0x40022
  5c:	01078793          	addi	x15,x15,16 # 0x40022010
  60:	08000713          	li	x14,128
  64:	00e7a023          	sw	x14,0(x15)
  68:	e000f7b7          	lui	x15,0xe000f
  6c:	d1078793          	addi	x15,x15,-752 # 0xe000ed10
  70:	80000737          	lui	x14,0x80000
  74:	00e7a023          	sw	x14,0(x15)
