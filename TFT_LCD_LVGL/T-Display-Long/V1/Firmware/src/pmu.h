#pragma once

// SY6970 charger / power path on the T-Display-S3-Long (I2C 0x6A on the touch bus).
// Left at its defaults it limits USB input current and keeps hunting for a battery.
// Call pmuBegin() after touchBegin(), since that starts Wire1.
void pmuBegin();
// Re-checks the registers now and then and puts them back if the chip reset them.
void pmuLoop();
