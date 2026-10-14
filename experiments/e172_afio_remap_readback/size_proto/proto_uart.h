/* Experiment: USART binding at begin() vs at build time, per-USART modes.
 *   PROTO_Un = 0 unused, 1 bound at begin() (shared IRQ, route table), 2 fixed at build time */
#pragma once
#include <Arduino.h>
#include "ch32rv_registers.h"
#include "ch32rv_gpio.h"

#ifndef PROTO_U1
#define PROTO_U1 0
#endif
#ifndef PROTO_U2
#define PROTO_U2 0
#endif
#ifndef PROTO_U3
#define PROTO_U3 0
#endif
#ifndef PROTO_U4
#define PROTO_U4 0
#endif
#ifndef PROTO_U5
#define PROTO_U5 0
#endif

#define PROTO_RXSZ 64
#define PROTO_TXSZ 64

struct ProtoRing {
    volatile uint16_t head, tail;
    uint8_t buf[64];
};

/* State only: all zero at reset, so an object lives in .bss with no ctor. */
struct ProtoState {
    uint32_t base;          /* 0 = unbound */
    int8_t slot;
    ProtoRing rx, tx;
};

void proto_irq_body(ProtoState &s);
bool proto_hw_begin(ProtoState &s, uint8_t slot, uint8_t tx, uint8_t rx, uint32_t v1, uint32_t v2,
                    unsigned long baud);

class ProtoSerial : public HardwareSerial {
public:
    void begin(unsigned long baud) override { begin(baud, SERIAL_8N1); }
    void begin(unsigned long baud, uint16_t) override { (void)baud; }   /* no pins: unbound */
    /* bound here: (rx, tx) -> USART + route, from the enabled USARTs only */
    bool begin(unsigned long baud, uint16_t config, int rx, int tx);
    void end() override;
    int available() override;
    int peek() override;
    int read() override;
    void flush() override;
    size_t write(uint8_t c) override;
    using Print::write;
    operator bool() override { return _s.base != 0; }
    int peripheral() const { return _s.base ? _s.slot + 1 : 0; }
    ProtoState _s;
};

/* build-time fixed: the USART number is a template argument */
template <int N> class ProtoFixedSerial : public HardwareSerial {
public:
    void begin(unsigned long baud) override;
    void begin(unsigned long baud, uint16_t) override { begin(baud); }
    void end() override {}
    int available() override { return (uint16_t)(s.rx.head - s.rx.tail) % PROTO_RXSZ; }
    int peek() override { return s.rx.head == s.rx.tail ? -1 : s.rx.buf[s.rx.tail]; }
    int read() override {
        if (s.rx.head == s.rx.tail) return -1;
        uint8_t c = s.rx.buf[s.rx.tail];
        s.rx.tail = (s.rx.tail + 1) % PROTO_RXSZ;
        return c;
    }
    void flush() override { while (s.tx.head != s.tx.tail) {} }
    size_t write(uint8_t c) override;
    using Print::write;
    operator bool() override { return s.base != 0; }
    static ProtoState s;
};
