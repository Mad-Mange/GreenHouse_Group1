#pragma once

enum adc
{
	ADC0 = 0,
	ADC1,
	ADC_COUNT
};

int adc_init();
uint16_t adc_single();
void adc_print_value(enum adc, uint16_t raw_value);
void adc_command(int adc);
void adc_trig_thread(void *parameters);
