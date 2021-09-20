#pragma once

int lin_init();
void lin_command();
void lin_off();
void lin_on();
void lin_send(int id);
void lin_thread(void *parameters);
