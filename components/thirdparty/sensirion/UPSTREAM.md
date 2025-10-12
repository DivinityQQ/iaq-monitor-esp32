Sensirion Gas Index Algorithm (VOC/NOx)
======================================

This directory vendors Sensirion's Gas Index Algorithm for SGP40/SGP41.

Included files:
- sensirion_gas_index_algorithm.c
- include/sensirion_gas_index_algorithm.h

Source: Sensirion public repository (gas-index-algorithm)
License: BSD-3-Clause (see headers in source files)

Integration notes:
- The driver (sgp41_driver.c) uses our native ESP-IDF I2C and feeds SRAW_VOC/NOx
  to Sensirion's algorithm at the configured cadence (1s recommended).
- Warm-up consists of SGP41 conditioning (~10s, clamped) and algorithm blackout
  (~45s). The coordinator treats this combined period as WARMING.

