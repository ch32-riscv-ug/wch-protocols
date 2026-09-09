// E030 reuses the E021 spool path with a 16 MiB destination at 20 MHz.
#define EXPERIMENT_ID "E030"
#define SAMPLE_RATE_HZ 20000000
#define CAPTURE_BUFFER_SIZE (16 * 1024 * 1024)
#define EXPERIMENT_RUNS 1
#include "../e021_p4_parlio_psram_spool/e021_p4_parlio_psram_spool.ino"
