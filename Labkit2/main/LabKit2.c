#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/can.h"

/* --------------------- Definitions and static variables ------------------ */
#define DATA_PERIOD_MS                  50
#define NO_OF_ITERS                     3
#define ITER_DELAY_MS                   1000
#define RX_TASK_PRIO                    8       //Receiving task priority
#define TX_TASK_PRIO                    9       //Sending task priority
#define CTRL_TSK_PRIO                   10      //Control task priority
#define TX_GPIO_NUM                     0
#define RX_GPIO_NUM                     2
#define EXAMPLE_TAG                     "Lab Kit 2"

#define ID_MASTER_HMI_NODE              0x102
#define ID_SLAVE_HMI_RESP               0x102
#define ID_MASTER_LCD_NODE              0x103
#define ID_SLAVE_LCD_RESP               0x103

typedef enum {
    TX_SEND_HMI_NODE,
    TX_SEND_LCD_NODE,
    TX_TASK_EXIT,actions
} tx_task_action_t;

typedef enum {
    RX_RECEIVE_HMI_NODE,
    RX_RECEIVE_LCD_NODE,
    RX_TASK_EXIT,
} rx_task_action_t;

static const can_general_config_t g_config = CAN_GENERAL_CONFIG_DEFAULT(TX_GPIO_NUM, RX_GPIO_NUM, CAN_MODE_NORMAL);
static const can_timing_config_t t_config = CAN_TIMING_CONFIG_1MBITS();
static const can_filter_config_t f_config = CAN_FILTER_CONFIG_ACCEPT_ALL();

static const can_message_t hmo_resp = {.identifier = ID_SLAVE_HMI_RESP, .data_length_code = 8,
                                        .flags = CAN_MSG_FLAG_NONE, .data = {10, 20 , 0 , 0 ,0 ,50 ,100 ,255}};

static const can_message_t lcd_resp = {.identifier = ID_SLAVE_LCD_RESP, .data_length_code = 8,
                                        .flags = CAN_MSG_FLAG_NONE, .data = {255, 0 , 0 , 255 ,0 ,0 ,0 ,0}};

static QueueHandle_t tx_task_queue;
static QueueHandle_t rx_task_queue;
static SemaphoreHandle_t ctrl_task_sem;
static SemaphoreHandle_t stop_data_sem;
static SemaphoreHandle_t done_sem;

/* --------------------------- Listen for Frame -------------------------- */
static void can_receive_task(void *arg)
{
    while (1) {
        rx_task_action_t action;
        xQueueReceive(rx_task_queue, &action, portMAX_DELAY);

        if (action == RX_RECEIVE_HMI_NODE) {
            //Listen for specific frames from Lab Kit 1
            can_message_t rx_msg;
            while (1) {
                can_receive(&rx_msg, portMAX_DELAY);
                if (rx_msg.identifier == ID_MASTER_HMI_NODE) {
                    xSemaphoreGive(ctrl_task_sem);
                    break;
                }
            }
        } else if (action == RX_RECEIVE_LCD_NODE) {
            //Listen for specific frames from Lab Kit 1
            can_message_t rx_msg;
            while (1) {
                can_receive(&rx_msg, portMAX_DELAY);
                if (rx_msg.identifier == ID_MASTER_LCD_NODE) {
                    xSemaphoreGive(ctrl_task_sem);
                    break;
                }
            }
        } 
    }
    vTaskDelete(NULL);
}
/* --------------------------- Sending Back Frame -------------------------- */
static void can_transmit_task(void *arg)
{
    while (1) {
        tx_task_action_t action;
        xQueueReceive(tx_task_queue, &action, portMAX_DELAY);

        if (action == TX_SEND_HMI_NODE) {
            //Transmit response to Lab Kit 2
            can_transmit(&hmo_resp, portMAX_DELAY);
            ESP_LOGI(EXAMPLE_TAG, "Sending back frame...");
            xSemaphoreGive(ctrl_task_sem);
        } 
        else if (action == TX_SEND_LCD_NODE) {
            //Transmit response to Lab Kit 2
            can_transmit(&lcd_resp, portMAX_DELAY);
            ESP_LOGI(EXAMPLE_TAG, "Sending back frame...");
            xSemaphoreGive(ctrl_task_sem);
        } 
    }
    vTaskDelete(NULL);
}
/* --------------------------- Connect Listen/Sending -------------------------- */
static void can_control_task(void *arg)
{
    xSemaphoreTake(ctrl_task_sem, portMAX_DELAY);
    tx_task_action_t tx_action;
    rx_task_action_t rx_action;

    // for (int iter = 0; iter < NO_OF_ITERS; iter++) {
    while(true) {
        ESP_ERROR_CHECK(can_start());
        ESP_LOGI(EXAMPLE_TAG, "Listens for packages");

        //Listen of frame from Lab Kit 1
        rx_action = RX_RECEIVE_HMI_NODE;
        xQueueSend(rx_task_queue, &rx_action, portMAX_DELAY);
        xSemaphoreTake(ctrl_task_sem, portMAX_DELAY);
        //Send frame back to Lab Kit 1
        tx_action = TX_SEND_HMI_NODE;
        xQueueSend(tx_task_queue, &tx_action, portMAX_DELAY);
        xSemaphoreTake(ctrl_task_sem, portMAX_DELAY);

        //Listen of frame from Lab Kit 1
        rx_action = RX_RECEIVE_LCD_NODE;
        xQueueSend(rx_task_queue, &rx_action, portMAX_DELAY);
        xSemaphoreTake(ctrl_task_sem, portMAX_DELAY);
        //Send frame back to Lab Kit 1
        tx_action = TX_SEND_LCD_NODE;
        xQueueSend(tx_task_queue, &tx_action, portMAX_DELAY);
        xSemaphoreTake(ctrl_task_sem, portMAX_DELAY);


        //Wait for bus to become free
        can_status_info_t status_info;
        can_get_status_info(&status_info);
        while (status_info.msgs_to_tx > 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            can_get_status_info(&status_info);
        }

        ESP_ERROR_CHECK(can_stop());
        ESP_LOGI(EXAMPLE_TAG, "Stop sending packages");
        vTaskDelay(pdMS_TO_TICKS(ITER_DELAY_MS));
    }

    //Stop TX and RX tasks
    tx_action = TX_TASK_EXIT;
    rx_action = RX_TASK_EXIT;
    xQueueSend(tx_task_queue, &tx_action, portMAX_DELAY);
    xQueueSend(rx_task_queue, &rx_action, portMAX_DELAY);

    //Delete Control task
    xSemaphoreGive(done_sem);
    vTaskDelete(NULL);
}
/* --------------------------- Main Function -------------------------- */
void app_main()
{
    //Add short delay to allow master it to initialize first
    for (int i = 3; i > 0; i--) {
        printf("Lab Kit 2 starting in %d\n", i);
        vTaskDelay(pdMS_TO_TICKS(500));
    }


    //Create semaphores and tasks
    tx_task_queue = xQueueCreate(1, sizeof(tx_task_action_t));
    rx_task_queue = xQueueCreate(1, sizeof(rx_task_action_t));
    ctrl_task_sem = xSemaphoreCreateBinary();
    stop_data_sem  = xSemaphoreCreateBinary();;
    done_sem  = xSemaphoreCreateBinary();;
    xTaskCreatePinnedToCore(can_receive_task, "CAN_rx", 4096, NULL, RX_TASK_PRIO, NULL, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(can_transmit_task, "CAN_tx", 4096, NULL, TX_TASK_PRIO, NULL, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(can_control_task, "CAN_ctrl", 4096, NULL, CTRL_TSK_PRIO, NULL, tskNO_AFFINITY);

    //Install CAN driver, trigger tasks to start
    ESP_ERROR_CHECK(can_driver_install(&g_config, &t_config, &f_config));
    ESP_LOGI(EXAMPLE_TAG, "Driver installed and started");

    xSemaphoreGive(ctrl_task_sem);              //Start Control task
    xSemaphoreTake(done_sem, portMAX_DELAY);    //Wait for tasks to complete

    //Uninstall CAN driver
    ESP_ERROR_CHECK(can_driver_uninstall());
    ESP_LOGI(EXAMPLE_TAG, "Driver uninstalled");

    //Cleanup
    vSemaphoreDelete(ctrl_task_sem);
    vSemaphoreDelete(stop_data_sem);
    vSemaphoreDelete(done_sem);
    vQueueDelete(tx_task_queue);
    vQueueDelete(rx_task_queue);
}