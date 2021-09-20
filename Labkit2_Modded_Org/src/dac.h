#pragma once

enum dac
{
	DAC0 = 0,
	DAC1,
	DAC_COUNT
};

int dac_init();
void dac_command(int dac);
