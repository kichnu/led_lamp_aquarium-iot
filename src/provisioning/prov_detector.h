#ifndef PROV_DETECTOR_H
#define PROV_DETECTOR_H

#include <Arduino.h>

// ===============================
// PROVISIONING BUTTON DETECTOR
// ===============================

/**
 * Check if provisioning button is held during boot
 * Must be called EARLY in setup() before other GPIO init
 * 
 * @return true if button held >5s, false otherwise
 */
bool checkProvisioningButton();

/**
 * Diagnostyka przycisku w trakcie pracy (wywołuj w loop()): loguje naciśnięcie
 * i zwolnienie z czasem trzymania. Tylko log — przycisk w pracy nic nie robi.
 */
void updateProvisioningButtonLog();

#endif