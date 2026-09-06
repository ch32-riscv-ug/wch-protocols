# b003stub-ch5xx_write_safe  (64 bytes)
# riscv-none-elf-objdump -D -b binary -m riscv:rv32 -M numeric
   0:	04450693          	addi	x13,x10,68
   4:	4298                	lw	x14,0(x13)
   6:	42dc                	lw	x15,4(x13)
   8:	4690                	lw	x12,8(x13)
   a:	400016b7          	lui	x13,0x40001
   e:	04068693          	addi	x13,x13,64 # 0x40001040
  12:	05700293          	li	x5,87
  16:	0a800313          	li	x6,168
  1a:	00568023          	sb	x5,0(x13)
  1e:	00668023          	sb	x6,0(x13)
  22:	c611                	beqz	x12,0x2e
  24:	00065863          	bgez	x12,0x34
  28:	00e68223          	sb	x14,4(x13)
  2c:	a029                	j	0x36
  2e:	00e69223          	sh	x14,4(x13)
  32:	a011                	j	0x36
  34:	c2d8                	sw	x14,4(x13)
  36:	00068023          	sb	x0,0(x13)
  3a:	56fd                	li	x13,-1
  3c:	c114                	sw	x13,0(x10)
  3e:	8082                	ret
