/*  Copyright (C) 2021 jkroso Jake Rosoman
    https://github.com/jkroso/pico-gpio-interrupt.c
*/

#ifndef PICO_GPIO_INTERRUPT_C
#define PICO_GPIO_INTERRUPT_C

#include "pico/stdlib.h"
#include <stdlib.h>

typedef void (*handler)(void *argument);

typedef struct {
  void * argument;
  handler fn;
} closure_t;

closure_t handlers[28] = {NULL};

void handle_interupt(uint gpio, uint32_t events) {
  closure_t handler = handlers[gpio];
  handler.fn(handler.argument);
}

void listen(uint pin, int condition, handler fn, void *arg) {
  gpio_init(pin);
  gpio_pull_up(pin);
  gpio_set_irq_enabled_with_callback(pin, condition, true, handle_interupt);
  closure_t *handler = malloc(sizeof(closure_t));
  handler->argument = arg;
  handler->fn = fn;
  handlers[pin] = *handler;
}

#endif