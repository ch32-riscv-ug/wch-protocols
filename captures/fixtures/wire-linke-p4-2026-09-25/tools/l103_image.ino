// Scratch image for the LinkE wire captures: a counter the debugger can see change (halt / step / regs).
volatile uint32_t counter;
void setup() {}
void loop() { ++counter; }
