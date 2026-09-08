// E026 stops a 20 MHz capture after the requested post-trigger samples.
#include <Arduino.h>

#define EXPERIMENT_ID "E026"
#define SAMPLE_RATE_HZ 20000000
#define TRIGGER_MODE_FOR_RUN(run) 1
#define PRE_SAMPLES_FOR_RUN(run) (131072U + (run) * 131072U)
#define POST_SAMPLES_FOR_RUN(run) (393216U - (run) * 131072U)
#define TRIGGER_MIN_INDEX_FOR_RUN(run) PRE_SAMPLES_FOR_RUN(run)
#define CAPTURE_COMPLETE(copied)                                      \
  (trigger_test::metrics.found &&                                    \
   (copied) >= trigger_test::metrics.first_index +                   \
                   POST_SAMPLES_FOR_RUN(trigger_test::metrics.run))
#include "../e024_p4_sump_swar_trigger_80mhz/e024_p4_sump_swar_trigger_80mhz.ino"
