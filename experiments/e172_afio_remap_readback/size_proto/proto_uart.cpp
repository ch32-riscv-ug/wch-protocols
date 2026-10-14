#include "proto_uart.h"

/* ---- per-USART constants from the variant (only for enabled USARTs) ---- */
struct ProtoDesc {
    uint32_t base, clk_addr, clk_mask, rm1, rm2;
    uint8_t irqn, slot;
};
#define PROTO_RM1(n) CH32RV_SERIAL##n##_REMAP_MASK
#ifndef CH32RV_SERIAL1_REMAP_MASK
#define CH32RV_SERIAL1_REMAP_MASK 0u
#endif
#ifndef CH32RV_SERIAL2_REMAP_MASK
#define CH32RV_SERIAL2_REMAP_MASK 0u
#endif
#ifndef CH32RV_SERIAL3_REMAP_MASK
#define CH32RV_SERIAL3_REMAP_MASK 0u
#endif
#ifndef CH32RV_SERIAL4_REMAP_MASK
#define CH32RV_SERIAL4_REMAP_MASK 0u
#endif
#ifndef CH32RV_SERIAL5_REMAP_MASK
#define CH32RV_SERIAL5_REMAP_MASK 0u
#endif
#ifndef CH32RV_SERIAL1_REMAP2_MASK
#define CH32RV_SERIAL1_REMAP2_MASK 0u
#endif
#ifndef CH32RV_SERIAL2_REMAP2_MASK
#define CH32RV_SERIAL2_REMAP2_MASK 0u
#endif
#ifndef CH32RV_SERIAL3_REMAP2_MASK
#define CH32RV_SERIAL3_REMAP2_MASK 0u
#endif
#ifndef CH32RV_SERIAL4_REMAP2_MASK
#define CH32RV_SERIAL4_REMAP2_MASK 0u
#endif
#ifndef CH32RV_SERIAL5_REMAP2_MASK
#define CH32RV_SERIAL5_REMAP2_MASK 0u
#endif
#define PROTO_DESC(n) { CH32RV_USART##n##_BASE, CH32RV_SERIAL##n##_CLKEN_ADDR, CH32RV_SERIAL##n##_CLKEN_MASK, \
                        CH32RV_SERIAL##n##_REMAP_MASK, CH32RV_SERIAL##n##_REMAP2_MASK, CH32RV_SERIAL##n##_IRQ, (n) - 1 }

/* ---- shared low level ---- */
__attribute__((always_inline)) static inline void irq_inline(ProtoState &s)
{
    const uint32_t b = s.base;
    const uint16_t st = CH32RV_USART_STATR(b);
    if (st & CH32RV_USART_STATR_RXNE) {
        const uint8_t c = (uint8_t)CH32RV_USART_DATAR(b);
        const uint16_t n = (s.rx.head + 1) % PROTO_RXSZ;
        if (n != s.rx.tail) { s.rx.buf[s.rx.head] = c; s.rx.head = n; }
    }
    if ((st & CH32RV_USART_STATR_TXE) && (CH32RV_USART_CTLR1(b) & CH32RV_USART_CTLR1_TXEIE)) {
        if (s.tx.head == s.tx.tail) {
            CH32RV_USART_CTLR1(b) &= (uint16_t)~CH32RV_USART_CTLR1_TXEIE;
        } else {
            CH32RV_USART_DATAR(b) = s.tx.buf[s.tx.tail];
            s.tx.tail = (s.tx.tail + 1) % PROTO_TXSZ;
        }
    }
}
void proto_irq_body(ProtoState &s) { irq_inline(s); }

#ifdef PROTO_CHECK
/* readback check: CNF bits [3:2] and "MODE != 0" only - some families implement
 * fewer MODE (speed) bits (V006: 3 reads back as 1) */
static bool cfg_ok(uint8_t pin, uint32_t want)
{
    const uint8_t port = (uint8_t)CH32RV_PIN_PORT(pin), bit = (uint8_t)CH32RV_PIN_BIT(pin);
    const uint32_t got = ((bit < 8 ? CH32RV_GPIO_CFGLR(port) : CH32RV_GPIO_CFGHR(port)) >> ((bit & 7) * 4)) & 0xFu;
    return (got & 0xCu) == (want & 0xCu) && ((got & 3u) != 0) == ((want & 3u) != 0);
}
#endif

static bool hw_start(ProtoState &s, const ProtoDesc &d, uint8_t tx, uint8_t rx, uint32_t v1, uint32_t v2,
                     unsigned long baud)
{
    s.base = d.base;
    s.slot = (int8_t)d.slot;
    ch32rv_clock_enable_at(d.clk_addr, d.clk_mask);
    ch32rv_clock_enable(AFIO);
    if (d.rm1) CH32RV_AFIO_PCFR1 = (CH32RV_AFIO_PCFR1 & ~d.rm1) | v1;
    if (d.rm2) CH32RV_AFIO_PCFR2 = (CH32RV_AFIO_PCFR2 & ~d.rm2) | v2;
#if defined(PROTO_CHECK) || defined(PROTO_CHECK_REMAP)
    if ((CH32RV_AFIO_PCFR1 & d.rm1) != v1 || (CH32RV_AFIO_PCFR2 & d.rm2) != v2) { s.base = 0; return false; }
#endif
    ch32rv_gpio_clock_enable((uint8_t)CH32RV_PIN_PORT(tx));
    ch32rv_gpio_clock_enable((uint8_t)CH32RV_PIN_PORT(rx));
    ch32rv_gpio_set_config((uint8_t)CH32RV_PIN_PORT(tx), (uint8_t)CH32RV_PIN_BIT(tx), CH32RV_GPIO_CFG_AF_PP_50M);
    ch32rv_gpio_set_config((uint8_t)CH32RV_PIN_PORT(rx), (uint8_t)CH32RV_PIN_BIT(rx), CH32RV_GPIO_CFG_IN_PULL);
    ch32rv_gpio_set((uint8_t)CH32RV_PIN_PORT(rx), (uint8_t)CH32RV_PIN_BIT(rx));
#ifdef PROTO_CHECK
    if (!cfg_ok(tx, CH32RV_GPIO_CFG_AF_PP_50M) || !cfg_ok(rx, CH32RV_GPIO_CFG_IN_PULL)) { s.base = 0; return false; }
#endif
    CH32RV_USART_BRR(d.base) = (uint16_t)((F_CPU + baud / 2) / baud);
    CH32RV_USART_CTLR2(d.base) = 0;
    CH32RV_USART_CTLR3(d.base) = 0;
    CH32RV_USART_CTLR1(d.base) = CH32RV_USART_CTLR1_TE | CH32RV_USART_CTLR1_RE | CH32RV_USART_CTLR1_RXNEIE |
                                 CH32RV_USART_CTLR1_UE;
    ch32rv_irq_enable(d.irqn);
    return true;
}

static size_t tx_put(ProtoState &s, uint8_t c)
{
    const uint16_t n = (s.tx.head + 1) % PROTO_TXSZ;
    while (n == s.tx.tail) {}
    s.tx.buf[s.tx.head] = c;
    s.tx.head = n;
    CH32RV_USART_CTLR1(s.base) |= CH32RV_USART_CTLR1_TXEIE;
    return 1;
}

/* ======================= mode 1: bound at begin() ======================= */
#define PROTO_ANY_RUNTIME (PROTO_U1 == 1 || PROTO_U2 == 1 || PROTO_U3 == 1 || PROTO_U4 == 1 || PROTO_U5 == 1)
#if PROTO_ANY_RUNTIME

struct ProtoSlotRoutes {
    ProtoDesc d;
    const ch32rv_route_t *rows;
    uint8_t count;
};

/* route rows, or one fixed row for a USART with no AFIO field */
#define PROTO_ROWS_OR_FIXED(n)                                                              \
    static const ch32rv_route_t rows##n[] = PROTO_ROUTES_##n;
#if defined(CH32RV_SERIAL1_ROUTES)
#define PROTO_ROUTES_1 CH32RV_SERIAL1_ROUTES
#else
#define PROTO_ROUTES_1 { { 0, { CH32RV_SERIAL1_TX, CH32RV_SERIAL1_RX, CH32RV_ROUTE_NO_PIN }, 0u, 0u } }
#endif
#if defined(CH32RV_SERIAL2_ROUTES)
#define PROTO_ROUTES_2 CH32RV_SERIAL2_ROUTES
#else
#define PROTO_ROUTES_2 { { 0, { CH32RV_SERIAL2_TX, CH32RV_SERIAL2_RX, CH32RV_ROUTE_NO_PIN }, 0u, 0u } }
#endif
#if defined(CH32RV_SERIAL3_ROUTES)
#define PROTO_ROUTES_3 CH32RV_SERIAL3_ROUTES
#else
#define PROTO_ROUTES_3 { { 0, { CH32RV_SERIAL3_TX, CH32RV_SERIAL3_RX, CH32RV_ROUTE_NO_PIN }, 0u, 0u } }
#endif
#if defined(CH32RV_SERIAL4_ROUTES)
#define PROTO_ROUTES_4 CH32RV_SERIAL4_ROUTES
#else
#define PROTO_ROUTES_4 { { 0, { CH32RV_SERIAL4_TX, CH32RV_SERIAL4_RX, CH32RV_ROUTE_NO_PIN }, 0u, 0u } }
#endif
#if defined(CH32RV_SERIAL5_ROUTES)
#define PROTO_ROUTES_5 CH32RV_SERIAL5_ROUTES
#else
#define PROTO_ROUTES_5 { { 0, { CH32RV_SERIAL5_TX, CH32RV_SERIAL5_RX, CH32RV_ROUTE_NO_PIN }, 0u, 0u } }
#endif

#if PROTO_U1 == 1
PROTO_ROWS_OR_FIXED(1)
#endif
#if PROTO_U2 == 1
PROTO_ROWS_OR_FIXED(2)
#endif
#if PROTO_U3 == 1
PROTO_ROWS_OR_FIXED(3)
#endif
#if PROTO_U4 == 1
PROTO_ROWS_OR_FIXED(4)
#endif
#if PROTO_U5 == 1
PROTO_ROWS_OR_FIXED(5)
#endif
#define PROTO_SLOT(n) { PROTO_DESC(n), rows##n, (uint8_t)(sizeof rows##n / sizeof rows##n[0]) },
static const ProtoSlotRoutes slots[] = {
#if PROTO_U1 == 1
    PROTO_SLOT(1)
#endif
#if PROTO_U2 == 1
    PROTO_SLOT(2)
#endif
#if PROTO_U3 == 1
    PROTO_SLOT(3)
#endif
#if PROTO_U4 == 1
    PROTO_SLOT(4)
#endif
#if PROTO_U5 == 1
    PROTO_SLOT(5)
#endif
};
#define NSLOTS (sizeof slots / sizeof slots[0])
static ProtoState *owner[NSLOTS];

bool ProtoSerial::begin(unsigned long baud, uint16_t config, int rx, int tx)
{
    (void)config;
    end();
    for (uint8_t i = 0; i < NSLOTS; i++) {
        if (owner[i]) continue;                       /* another object holds it */
        const ProtoSlotRoutes &sr = slots[i];
        for (uint8_t r = 0; r < sr.count; r++) {
            const ch32rv_route_t &row = sr.rows[r];
            if ((tx < 0 || row.pins[0] == tx) && (rx < 0 || row.pins[1] == rx)) {
                if (!hw_start(_s, sr.d, row.pins[0], row.pins[1], row.value, row.value2, baud))
                    return false;
                owner[i] = &_s;
                _s.slot = (int8_t)i;
                return true;
            }
        }
    }
    return false;
}

void ProtoSerial::end()
{
    if (!_s.base) return;
    CH32RV_USART_CTLR1(_s.base) = 0;
    owner[(uint8_t)_s.slot] = nullptr;
    _s.base = 0;
}

/* one handler for every enabled USART vector; mcause says which */
extern "C" __attribute__((interrupt)) void proto_usart_irq(void)
{
    uint32_t cause;
    __asm__ volatile("csrr %0, mcause" : "=r"(cause));
    const uint8_t irqn = (uint8_t)(cause & 0xFFFu);
    for (uint8_t i = 0; i < NSLOTS; i++) {
        if (slots[i].d.irqn == irqn) {
            if (owner[i]) proto_irq_body(*owner[i]);
            return;
        }
    }
}
#define PROTO_ALIAS(n) extern "C" void CH32RV_SERIAL##n##_HANDLER(void) __attribute__((alias("proto_usart_irq")));
#if PROTO_U1 == 1
PROTO_ALIAS(1)
#endif
#if PROTO_U2 == 1
PROTO_ALIAS(2)
#endif
#if PROTO_U3 == 1
PROTO_ALIAS(3)
#endif
#if PROTO_U4 == 1
PROTO_ALIAS(4)
#endif
#if PROTO_U5 == 1
PROTO_ALIAS(5)
#endif

int ProtoSerial::available() { return (uint16_t)(_s.rx.head - _s.rx.tail) % PROTO_RXSZ; }
int ProtoSerial::peek() { return _s.rx.head == _s.rx.tail ? -1 : _s.rx.buf[_s.rx.tail]; }
int ProtoSerial::read()
{
    if (_s.rx.head == _s.rx.tail) return -1;
    const uint8_t c = _s.rx.buf[_s.rx.tail];
    _s.rx.tail = (_s.rx.tail + 1) % PROTO_RXSZ;
    return c;
}
void ProtoSerial::flush() { while (_s.tx.head != _s.tx.tail) {} }
size_t ProtoSerial::write(uint8_t c) { return _s.base ? tx_put(_s, c) : 0; }
#endif /* PROTO_ANY_RUNTIME */

/* ======================= mode 2: fixed at build time ======================= */
template <int N> ProtoState ProtoFixedSerial<N>::s;
template <int N> size_t ProtoFixedSerial<N>::write(uint8_t c) { return s.base ? tx_put(s, c) : 0; }

#define PROTO_FIXED(n)                                                                     \
    template <> void ProtoFixedSerial<n>::begin(unsigned long baud)                         \
    {                                                                                       \
        static const ProtoDesc d = PROTO_DESC(n);                                           \
        hw_start(s, d, CH32RV_SERIAL##n##_TX, CH32RV_SERIAL##n##_RX,                        \
                 PROTO_FIXED_V1_##n, PROTO_FIXED_V2_##n, baud);                             \
    }                                                                                       \
    template class ProtoFixedSerial<n>;                                                     \
    extern "C" __attribute__((interrupt)) void CH32RV_SERIAL##n##_HANDLER(void)             \
    {                                                                                       \
        irq_inline(ProtoFixedSerial<n>::s);                                                 \
    }
#ifndef CH32RV_SERIAL1_REMAP_VAL
#define CH32RV_SERIAL1_REMAP_VAL 0u
#endif
#ifndef CH32RV_SERIAL4_REMAP_VAL
#define CH32RV_SERIAL4_REMAP_VAL 0u
#endif
#define PROTO_FIXED_V1_1 CH32RV_SERIAL1_REMAP_VAL
#define PROTO_FIXED_V2_1 0u
#define PROTO_FIXED_V1_4 CH32RV_SERIAL4_REMAP_VAL
#define PROTO_FIXED_V2_4 0u
#if PROTO_U1 == 2
PROTO_FIXED(1)
#endif
#if PROTO_U4 == 2
PROTO_FIXED(4)
#endif
