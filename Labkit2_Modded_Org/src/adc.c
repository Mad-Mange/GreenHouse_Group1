/*
 *  This file is part of SWT21 lab kit firmware.
 *
 *  SWT21 lab kit firmware is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  SWT21 lab kit firmware is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with SWT21 lab kit firmware.  If not, see <https://www.gnu.org/licenses/>.
 *
 *  Copyright 2021 Joachim Lublin, Binäs Teknik AB
 */

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <driver/adc.h>
#include <driver/i2s.h>
#include <nvs_flash.h>
#include <esp_task_wdt.h>

#include <string.h>

#include "periodic.h"
#include "errors.h"
#include "adc.h"
#include "hci.h"

int adc_channel[ADC_COUNT] =
{
	ADC1_CHANNEL_4,
	ADC1_CHANNEL_5
};

const int ADC_BITS = 12;

struct
{
	uint16_t v1x_0_2v;
	uint16_t v1x_2v;
	uint16_t v10x_2v;
	uint16_t v10x_20v;
	uint16_t period;
	uint16_t offset;
	uint8_t flags; /* initialized, raw, amp, timestamp */
} adc_config[ADC_COUNT];

const uint8_t ADC_FLAG_INIT = 1 << 0;
const uint8_t ADC_FLAG_RAW =  1 << 1;
const uint8_t ADC_FLAG_AMP10X = 1 << 2;

static QueueHandle_t i2s_queue;
static QueueHandle_t cmd_queue;

struct cmd_event
{
	uint8_t event;
	union
	{
		struct
		{
			uint32_t sample_rate;
			uint16_t m, n;
			uint8_t value;
		} trig;
	};
};

enum
{
	EVENT_CMD_TRIG_OFF = 0,
	EVENT_CMD_TRIG
};

int adc_init()
{
	esp_err_t err;

	adc1_config_width(ADC_WIDTH_BIT_12);

	nvs_handle_t nvs_handle;
	err = nvs_open("SWT21 Lab kit", NVS_READWRITE, &nvs_handle);
	if(err != ESP_OK)
		goto esp_err;

	cmd_queue = xQueueCreate(10, sizeof(struct cmd_event));
	if(!cmd_queue)
		goto esp_err;

	for(int i = 0; i < ADC_COUNT; i++)
	{
		int error_flag = 0;
		char parameter_name[25] = {0};
		uint32_t value;

		snprintf(parameter_name, 24, "adc%d_1x_0_2v", i);
		err = nvs_get_u32(nvs_handle, parameter_name, &value);
		if(err)
		{
			printf("ERR %s not configured\n", parameter_name);
			error_flag = 1;
		}
		adc_config[i].v1x_0_2v = value;

		snprintf(parameter_name, 24, "adc%d_1x_2v", i);
		err = nvs_get_u32(nvs_handle, parameter_name, &value);
		if(err)
		{
			printf("ERR %s not configured\n", parameter_name);
			error_flag = 1;
		}
		adc_config[i].v1x_2v = value;

		snprintf(parameter_name, 24, "adc%d_10x_2v", i);
		err = nvs_get_u32(nvs_handle, parameter_name, &value);
		if(err)
		{
			printf("ERR %s not configured\n", parameter_name);
			error_flag = 1;
		}
		adc_config[i].v10x_2v = value;

		snprintf(parameter_name, 24, "adc%d_10x_20v", i);
		err = nvs_get_u32(nvs_handle, parameter_name, &value);
		if(err)
		{
			printf("ERR %s not configured\n", parameter_name);
			error_flag = 1;
		}
		adc_config[i].v10x_20v = value;

		/* Set channel attenuation */
		adc1_config_channel_atten(adc_channel[i], ADC_ATTEN_DB_11);

		/* Mark as initialized if we did not have any errors */
		if(!error_flag)
			adc_config[0].flags |= ADC_FLAG_INIT;
	}

	nvs_close(nvs_handle);

	/*
	 * Install I2S driver for trigging. Single trig only.
	 * Read from separate thread.
	 */
	i2s_config_t i2s_config =
	{
		.mode = I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_ADC_BUILT_IN,
		.sample_rate = 16000,
		.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
		.channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
		.communication_format = I2S_COMM_FORMAT_I2S,
		.intr_alloc_flags = 0,
		.dma_buf_count = 10,
		.dma_buf_len = 1024,
		.use_apll = 0
	};

	i2s_driver_install(I2S_NUM_0, &i2s_config, 20, &i2s_queue);
	i2s_stop(I2S_NUM_0);

	return 0;

esp_err:
	printf("ERR ADC init failed!\n");
	return -1;
}

uint16_t adc_single(enum adc adc)
{
	return adc1_get_raw(adc_channel[adc]);
}

static void adc_trig_off()
{
	struct cmd_event event =
	{
		.event = EVENT_CMD_TRIG_OFF
	};

	xQueueSendToBack(cmd_queue, &event, 0);
}

static void adc_trig(uint8_t value, uint32_t sample_rate, uint16_t m, uint16_t n)
{
	struct cmd_event event =
	{
		.event = EVENT_CMD_TRIG,
		.trig.value = value,
		.trig.sample_rate = sample_rate,
		.trig.m = m,
		.trig.n = n
	};

	xQueueSendToBack(cmd_queue, &event, 0);
}

void adc_print_value(enum adc adc, uint16_t raw_value)
{
	if(adc_config[adc].flags & ADC_FLAG_RAW)
	{
		printf("ADC%d %d\n", adc, raw_value);
	}
	else
	{
		int amp = adc_config[adc].flags & ADC_FLAG_AMP10X;
		float value;

		if(amp)
			value = 2.0 +
			        18.0/(adc_config[adc].v10x_20v - adc_config[adc].v10x_2v) *
			        (raw_value - adc_config[adc].v10x_2v);
		else
			value = 0.2 +
			        1.8/(adc_config[adc].v1x_2v - adc_config[adc].v1x_0_2v) *
			        (raw_value - adc_config[adc].v1x_0_2v);

		printf("ADC%d %0.3f\n", adc, value);
	}
}

static void adc_send_trig_data(int start, uint8_t *data, int len)
{
	uint8_t buf[2048+30];
	int n = 0;

	n += sprintf((char*)buf, "ADC trig %d+%d\n", start, len);

	for(int i = 0; i < len; i++)
		n += sprintf((char*)buf + n, "%02x", data[i]);

	n += sprintf((char*)buf + n, "\n");

	hci_print_bytes(buf, n);
}

void adc_command(int adc)
{
	char *cmd = strtok(NULL, " ");

	/* Make sure we have a command */
	if(!cmd)
		goto einval;

	/*
	 * Compare commande against known commands and handle them, otherwise tell
	 * the user it was invalid.
	 */
	if(strcmp(cmd, "help") == 0)
	{
		printf("OK\n");
		printf(
			"Available commands:\n"
			"\n"
			"adc%d off - turn off periodic adc\n"
			"adc%d single - convert single value\n"
#if 0
	/* This is TODO */
			"adc%d periodic <period (ms)> [offset (ms)] - convert periodically\n"
#endif
			"adc0 trig <trig value> <sample rate> <m> <n> - wait for trigger and\n"
			"                                              convert m values before and\n"
			"                                              n values after\n"
			"adc0 trig off - disable trig\n"
			"adc%d config raw <on/off> - enable or disable raw values\n"
			"adc%d config 10x <on/off> - enable 10x, otherwise 1x\n"
			"\n", adc, adc, adc, adc, adc, adc);
	}
	else if(strcmp(cmd, "off") == 0)
	{
		adc_off();
	}
	else if(strcmp(cmd, "single") == 0)
	{
		adc_print_value(adc, adc_single(adc));
	}
#if 0
	/* This is TODO */
	else if(strcmp(cmd, "periodic") == 0)
	{
		int period;
		int offset;
		char *arg;

		/* Read period argument */
		arg = strtok(NULL, " ");
		if(!arg)
			goto einval;

		period = atoi(arg);

		if(period >= 1 << 16)
			goto einval;

		/* Read offset argument */
		arg = strtok(NULL, " ");

		if(arg)
			offset = atoi(arg);
		else
			offset = 0;

		/* Send command */
		adc_periodic(period, offset);
	}
#endif
	else if(strcmp(cmd, "test") == 0)
	{
		adc_off();
		adc_trig(128, 8000, 128, 128);
	}
	else if(strcmp(cmd, "trig") == 0)
	{
		const char *arg;
		int value;
		int sample_rate;
		int m, n;

		/* Only ADC0 supported */
		if(adc != 0)
			goto einval;

		/* Read value argument */
		arg = strtok(NULL, " ");
		if(!arg)
			goto einval;

		if(strcmp(arg, "off") == 0)
		{
			adc_trig_off();
			printf("OK\n");
			return;
		}

		value = atoi(arg);

		if(value < 0 || value > 255)
			goto einval;

		/* Read sample_rate argument */
		arg = strtok(NULL, " ");
		if(!arg)
			goto einval;

		sample_rate = atoi(arg);

		/* Empirical max: 1333328, min probably 2496 */
		if(sample_rate < 2496 || sample_rate > 1333328)
			goto einval;

		/* Read m argument */
		arg = strtok(NULL, " ");

		if(!arg)
			goto einval;

		m = atoi(arg);

		if(m < 0 || m > 1024)
			goto einval;

		/* Read n argument */
		arg = strtok(NULL, " ");

		if(!arg)
			goto einval;

		n = atoi(arg);

		if(n < 0 || n > 64*1024-1)
			goto einval;

		/* Send command */
		adc_off();
		adc_trig(value, sample_rate, m, n);
	}	else if(strcmp(cmd, "config") == 0)
	{
		/* Read config argument */
		const char *param = strtok(NULL, " ");
		if(!param)
			goto einval;

		if(strcmp(param, "raw") == 0)
		{
			/* on/off */
			char *arg;

			/* Read id argument */
			arg = strtok(NULL, " ");
			if(!arg)
				goto einval;

			if(strcmp(arg, "on") == 0)
			{
				adc_config[adc].flags |= ADC_FLAG_RAW;
				printf("OK\n");
				return;
			}
			else if(strcmp(arg, "off") == 0)
			{
				adc_config[adc].flags &= ~ADC_FLAG_RAW;
				printf("OK\n");
				return;
			}
			else
				goto einval;
		}

		else if(strcmp(param, "10x") == 0)
		{
			/* on/off */
			char *arg;

			/* Read id argument */
			arg = strtok(NULL, " ");
			if(!arg)
				goto einval;

			if(strcmp(arg, "on") == 0)
			{
				adc_config[adc].flags |= ADC_FLAG_AMP10X;
				printf("OK\n");
				return;
			}
			else if(strcmp(arg, "off") == 0)
			{
				adc_config[adc].flags &= ~ADC_FLAG_AMP10X;
				printf("OK\n");
				return;
			}
			else
				goto einval;
		}
	}
	else
		goto einval;

	return;

einval:
	printf(EINVAL);
	return;
}

void adc_trig_thread(void *parameters)
{
	esp_err_t err;
	uint8_t stored_values[2][1024];
	int current_buf = 0;
	uint8_t first_buf = 0; /* Keep track if we only received zero or one buf */
	uint16_t m = 0, n = 0;
	uint8_t value = 0;
	int trig_len = 0;

	/*
	 * <----------------buf 0------------><------------buf 1-------------->
	 *                          <-----m----->T<-----n----->
	 *                    <-----m----->T<-----n----->
	 *    <-----m----->T<-----n----->
	 *
	 * If m + n < buffer size then three cases on trig:
	 * - M extends into previous buffer
	 * - All fits inside current buffer
	 * - N extends into next buffer
	 */

	esp_task_wdt_delete(xTaskGetCurrentTaskHandle());

	enum
	{
		STATE_TRIG_OFF,
		STATE_TRIG_SEARCHING,
		STATE_TRIG_FOUND
	} state = STATE_TRIG_OFF;


	while(1)
	{
		/*
		 * QueueSet implementation causes abort() if it is ever becoming full
		 * so we do not use it
		 */
		i2s_event_t i2s_event;
		if(xQueueReceive(i2s_queue, &i2s_event, 10))
		{
			if(i2s_event.type == I2S_EVENT_RX_DONE)
			{
				size_t bytes_read;
				uint8_t buf[2048];
				i2s_read(I2S_NUM_0, buf, 2048, &bytes_read, 0);

				if(bytes_read == 0)
					break;

				/* Store transformed values */
				for(int i = 0; i < bytes_read/2; i += 2)
				{
					/*
					 * We need to change byte order, probably due to I2S
					 * FIFO being 32-bit and we read 16-bits at a time
					 */
					stored_values[current_buf][i] = ((buf[2*i+3] & 0xf) << 4) | (buf[2*i+2] >> 4);
					stored_values[current_buf][i+1] = ((buf[2*i+1] & 0xf) << 4) | (buf[2*i] >> 4);
				}

				if(state == STATE_TRIG_SEARCHING)
				{
					/* Search for trig condition */
					uint8_t v0;
					int i;

					int other_buf = 1 - current_buf;
					if(!first_buf)
					{
						i = 0;
						v0 = stored_values[other_buf][1023];
					}
					else
					{
						i = 1;
						v0 = stored_values[current_buf][0];
					}

					for(; i < 1024; i++)
					{
						uint8_t v1 = stored_values[current_buf][i];

						if(v1 == value ||
						   (v1 > value && v0 < value) ||
						   (v1 < value && v0 > value))
						{
							state = STATE_TRIG_FOUND;
							trig_len = 0;


							int len0 = m - i; /* Points in last buffer */
							int start = 0;
							if(len0 > 0)
							{
								if(!first_buf)
									adc_send_trig_data(0, stored_values[other_buf] + 1024 - len0, len0);
								trig_len += len0;
							}
							else
								start = i - m;

							if(m + n - trig_len > (1024-start))
							{
								adc_send_trig_data(
									trig_len,
									&stored_values[current_buf][start],
									1024-start);
								trig_len += 1024;
							}
							else
							{
								adc_send_trig_data(
									trig_len,
									&stored_values[current_buf][start],
									m + n - trig_len);

								i2s_stop(I2S_NUM_0);
								i2s_adc_disable(I2S_NUM_0);
								state = STATE_TRIG_OFF;
							}

							break;
						}

						v0 = v1;
					}
				}
				else if(state == STATE_TRIG_FOUND)
				{
					if(m + n - trig_len > 1024)
					{
						adc_send_trig_data(trig_len, stored_values[current_buf], 1024);
						trig_len += 1024;
					}
					else
					{
						adc_send_trig_data(trig_len, stored_values[current_buf], m + n - trig_len);

						i2s_stop(I2S_NUM_0);
						i2s_adc_disable(I2S_NUM_0);
						state = STATE_TRIG_OFF;
					}
				}

				/* Clear first_buf since other_buf is now always filled */
				first_buf = 0;

				/* Switch buffer to use */
				if(current_buf == 1)
					current_buf = 0;
				else
					current_buf += 1;
			}
		}

		struct cmd_event cmd_event;

		if(xQueueReceive(cmd_queue, &cmd_event, 0))
		{
			if(cmd_event.event == EVENT_CMD_TRIG_OFF)
			{
				i2s_stop(I2S_NUM_0);
				i2s_adc_disable(I2S_NUM_0);
				state = STATE_TRIG_OFF;
			}
			else if(cmd_event.event == EVENT_CMD_TRIG)
			{
				/* TODO: check if we're already triggering */
				i2s_set_adc_mode(ADC_UNIT_1, adc_channel[0]);

				err = i2s_set_clk(
					I2S_NUM_0,
					cmd_event.trig.sample_rate,
					16, I2S_CHANNEL_MONO);
				float clk = 16 * i2s_get_clk(I2S_NUM_0);
				printf("ADC0 clk: %f\n", clk);

				if(err != ESP_OK)
				{
					printf("ERR Trig settings error\n");
					return;
				}

				value = cmd_event.trig.value;
				m = cmd_event.trig.m;
				n = cmd_event.trig.n;
				first_buf = 1;

				memset(stored_values, 0, 2*1024);
				current_buf = 0;
				trig_len = 0;

				i2s_start(I2S_NUM_0);
				i2s_adc_enable(I2S_NUM_0); /* TODO: locks ADC */

				state = STATE_TRIG_SEARCHING;
			}
		}
	}
}
