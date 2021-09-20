#pragma once
/*
 * This threads runs every ms and schedules what to do in each millisecond
 * and if it is possible to send it over UART (max 90 % utilization for periodics)
 * An obvious improvment would be to use timers to read the actual data
 * but currently we implement th easiest and fastest implementation.
 */

void periodic_thread(void *parameters);
void adc_off();
void adc_periodic(uint16_t period, uint16_t offset);
void led_off(uint8_t state);
void led_blink(uint16_t period, uint16_t offset);
