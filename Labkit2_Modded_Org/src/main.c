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
#include <freertos/task.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_task_wdt.h>

#include "hci.h"
#include "uart.h"
#include "periodic.h"
#include "adc.h"
#include "dac.h"
#include "can.h"
#include "led.h"
#include "lin.h"

/* Firmware main, sets up running threads */
int app_main()
{
	/* Initialize NVS */
	esp_err_t ret = nvs_flash_init();

	if(ret == ESP_ERR_NVS_NO_FREE_PAGES ||
	   ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
	{
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	hci_init();
	adc_init();
	dac_init();
	can_init();
	led_init();
	lin_init();
	uart_init();

	xTaskCreatePinnedToCore(&hci_thread, "hci", 10000, NULL, 1, NULL, 0);
	xTaskCreatePinnedToCore(&periodic_thread, "periodic", 10000, NULL, 5, NULL, 0);
	xTaskCreatePinnedToCore(&adc_trig_thread, "adc_trig", 10000, NULL, 4, NULL, 0);
	xTaskCreatePinnedToCore(&can_rx_thread, "can", 10000, NULL, 4, NULL, 0);
	xTaskCreatePinnedToCore(&lin_thread, "lin", 10000, NULL, 4, NULL, 0);
	xTaskCreatePinnedToCore(&uart_thread, "uart", 10000, NULL, 4, NULL, 0);


	while(1)
	{
		esp_task_wdt_reset();
		vTaskDelay(100 / portTICK_PERIOD_MS);
	}

	return 0;
}
