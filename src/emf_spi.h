#ifndef EMF_SPI_H_
#define EMF_SPI_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int emf_spi_init(void);
void emf_spi_poll_debug(void);
bool emf_spi_frame_ready(void);
bool emf_spi_take_frame_received_event(void);
const uint8_t *emf_spi_payload(void);
void emf_spi_release_frame(void);

#endif /* EMF_SPI_H_ */
