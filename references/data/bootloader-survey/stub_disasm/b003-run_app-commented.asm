# b003-run_app-commented  (48 bytes)
# riscv-none-elf-objdump -D -b binary -m riscv:rv32 -M numeric
   0:	45670737          	lui	x14,0x45670
   4:	400227b7          	lui	x15,0x40022
   8:	12370713          	addi	x14,x14,291 # 0x45670123
   c:	d798                	sw	x14,40(x15)
   e:	cdef9737          	lui	x14,0xcdef9
  12:	9ab70713          	addi	x14,x14,-1621 # 0xcdef89ab
  16:	d798                	sw	x14,40(x15)
  18:	0007a623          	sw	x0,12(x15) # 0x4002200c
  1c:	08000713          	li	x14,128
  20:	cb98                	sw	x14,16(x15)
  22:	e000f7b7          	lui	x15,0xe000f
  26:	80000737          	lui	x14,0x80000
  2a:	d0e7a823          	sw	x14,-752(x15) # 0xe000ed10
  2e:	8082                	ret
