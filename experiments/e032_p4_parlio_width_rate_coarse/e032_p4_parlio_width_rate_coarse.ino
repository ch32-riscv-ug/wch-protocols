#define EXPERIMENT_ID "E032"
#define RUN_CAPTURE_CASES()                                      \
  do {                                                           \
    constexpr uint32_t rates[] = {                               \
        20000000, 40000000, 80000000, 120000000, 160000000,      \
    };                                                           \
    for (size_t width : kWidths) {                               \
      for (uint32_t rate : rates) run_case(width, rate);          \
    }                                                            \
  } while (0)
#include "../e031_p4_parlio_channel_width/e031_p4_parlio_channel_width.ino"
