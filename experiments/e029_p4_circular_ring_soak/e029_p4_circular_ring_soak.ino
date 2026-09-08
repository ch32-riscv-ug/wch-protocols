// E029 repeats E027's one/two/four-wrap captures ten times.
#define EXPERIMENT_ID "E029"
#define EXPERIMENT_RUNS 30
#define WRAPS_FOR_RUN(run) (1U << ((run) % 3))
#include "../e027_p4_sump_circular_pretrigger/e027_p4_sump_circular_pretrigger.ino"
