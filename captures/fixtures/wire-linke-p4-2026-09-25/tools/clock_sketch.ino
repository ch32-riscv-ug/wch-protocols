// Scratch image for the LinkE wire captures: runs at a PLL clock (f_cpu from the build) and keeps a counter,
// so reads of RCC_CFGR0 / FLASH_ACTLR before and after each LinkE operation show whether the LinkE re-clocks it.
volatile uint32_t counter;
void setup() {}
void loop() { ++counter; }
