// E092 changes only TinyUSB's vendor OUT transfer arm length. Reuse the exact
// E091 peer implementation so byte validation and application work stay fixed.
#include "../../e091_p4_bulk_direction_same_peer/device/device.ino"
