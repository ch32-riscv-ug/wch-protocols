# E157 X035 buffered flash writer, runs from the debug program buffer.
# Preloaded by abstract register writes: a0=&DMDATA0, a1=&DMDATA1 (holds the
# next flash address), a2=&FLASH_STATR (0x4002200C; CTLR is +4), a3=FTPG|BUFLOAD.
# Autoexec on DMDATA0: every host write of one data word runs this once.
    .option rvc
    .text
    lw   s0, 0(a1)        # address
    lw   s1, 0(a0)        # data word from DMDATA0
    sw   s1, 0(s0)        # store into the flash page buffer window
    sw   a3, 4(a2)        # CTLR = FTPG | BUFLOAD
1:  lw   a4, 0(a2)        # STATR
    andi a4, a4, 1        # BSY
    bnez a4, 1b
    addi s0, s0, 4
    sw   s0, 0(a1)        # DMDATA1 = next address
    ebreak
