#ifndef EMF_DATA_H_
#define EMF_DATA_H_

#include <stddef.h>
#include <stdint.h>

#define EMF_SAMPLE_COUNT 8000
#define EMF_PAYLOAD_SIZE (EMF_SAMPLE_COUNT * sizeof(uint16_t))

float indenter_speed(float *v, size_t len);
void emf_data_generate(uint8_t *dst, size_t len);

#endif /* EMF_DATA_H_ */
