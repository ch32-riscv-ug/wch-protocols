#define EXPERIMENT_ID "E033"
#define RUN_CAPTURE_CASES()                                      \
  do {                                                           \
    for (uint32_t rate = 84000000; rate <= 120000000; rate += 4000000) \
      run_case(8, rate);                                         \
    for (uint32_t rate = 44000000; rate <= 80000000; rate += 4000000)  \
      run_case(16, rate);                                        \
  } while (0)
#include "../e031_p4_parlio_channel_width/e031_p4_parlio_channel_width.ino"
