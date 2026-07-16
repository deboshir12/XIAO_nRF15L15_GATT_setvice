#ifndef EMF_UART_H_
#define EMF_UART_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int emf_uart_init(void);
size_t emf_uart_rx_len(void);
uint32_t emf_uart_error_count(void);
int emf_uart_last_error(void);
bool emf_uart_frame_ready(void);
bool emf_uart_take_frame_received_event(void);
const uint8_t *emf_uart_payload(void);
void emf_uart_release_frame(void);

#endif /* EMF_UART_H_ */
