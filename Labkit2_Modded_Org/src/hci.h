#pragma once

#define printf(...) hci_print_str(__VA_ARGS__)
void hci_print_str(const char *format, ...);
void hci_print_bytes(const uint8_t *data, int len);
int hci_alloc_tx_slot(uint16_t period, uint16_t bytes);
void hci_free_tx_slot(int tx_handle);
void hci_init();
void hci_thread(void *parameters);
