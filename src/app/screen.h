#pragma once

#include <Arduino.h>

struct MeasurementSnapshot;

/**
 * Power Meter — main screen
 *
 * Shows 4 channel cards (V, I, P, Temp) + ambient temp + WiFi info.
 * Single-page application — no app framework needed.
 */

// Lifecycle
void power_meter_init();   // create UI on active screen
void power_meter_hide();   // destroy UI (for future use)
void power_meter_loop();   // periodic update (called from main loop)

// Data accessor for web server
const MeasurementSnapshot &power_meter_get_data();

// Key handlers (currently unused, reserved)
void power_meter_key_up();
void power_meter_key_down();
void power_meter_key_enter();
void power_meter_key_back();

// Scope mode
void power_meter_scope_show();
void power_meter_scope_hide();
void power_meter_scope_next_ch();
