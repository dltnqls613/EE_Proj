#pragma once

// Each model supplies this configuration while sharing common/EE_Standard.
#define EE_DEFAULT_ID 1
#define EE_DEFAULT_NAME "EE_Pipette"
#define EE_UART_BAUD 1500000
#define EE_SERVO_ENABLE_LEVEL HIGH
#define EE_SERVO_MIN_US 500
#define EE_SERVO_MAX_US 2500
#define EE_SERVO0_INITIAL_US 680
#define EE_SERVO1_INITIAL_US 2200

#define PIPETTE_TIP_INSERT_US 680
#define PIPETTE_TIP_EXTRACT_US 2380
// Measured full-pull endpoint supplied by the user, in microseconds.
#ifndef PIPETTE_PISTON_PULL_US
#define PIPETTE_PISTON_PULL_US 500
#endif
#define PIPETTE_PISTON_PUSH_US 2200
#define PIPETTE_LIQUID_EXIT_US 2500
#define PIPETTE_MOVE_SECONDS 0.6f

// ADC_PIN is provisionally the mini-holder sensor. Calibrate polarity/threshold.
#define EE_HOLDER_THRESHOLD 2048
#define EE_HOLDER_PRESENT_ABOVE true

// Sensor type/calibration were not included in the starter project.
// Set C/count + offset for a linear temperature sensor. For NTC sensors replace
// temperature() with the correct resistance/temperature conversion first.
#define EE_TEMP_C_PER_COUNT 0.0f
#define EE_TEMP_OFFSET_C 0.0f
#define EE_TEMP_MAX_C 100.0f
// Signed HX711 raw counts per kg (0 = not calibrated); offset is the empty load.
#define EE_LOADCELL_COUNTS_PER_KG 0.0f
#define EE_LOADCELL_OFFSET 0
