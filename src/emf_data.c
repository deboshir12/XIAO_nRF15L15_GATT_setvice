#include "emf_data.h"

#include <math.h>

#include <zephyr/random/random.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float speed[EMF_SAMPLE_COUNT];

float indenter_speed(float *v, size_t len)
{
	const float g = 9.80665f;
	const float fs_mhz = 40.0f;
	const float fs = fs_mhz * 1000000.0f;
	const float ts = 1.0f / fs;
	const float g_ts = g * ts;
	const float t_full = 50.0f;
	const float t_emf = (float)len / fs_mhz;
	const float t_mg = (t_emf - t_full) / 2.0f;
	const float v_t0 = 2.05f;
	const float v_t2 = -1.0f;

	size_t t0_i = (size_t)(t_mg * fs_mhz + 1.0f);
	size_t t2_i = (size_t)((t_mg + t_full) * fs_mhz + 1.0f);

	if (t0_i > len) {
		t0_i = len;
	}

	if (t2_i > len) {
		t2_i = len;
	}

	float v_buff = v_t0 - g_ts;

	for (size_t i = t0_i; i-- > 0;) {
		v[i] = v_buff;
		v_buff -= g_ts;
	}

	float t_buff = 0.0f;
	float t_pi = (float)M_PI / (float)(t2_i - t0_i);
	float amp = (v_t0 - v_t2) / 2.0f;
	float offset = amp + v_t2;

	for (size_t i = t0_i; i < t2_i; ++i) {
		v[i] = amp * cosf(t_buff) + offset;
		t_buff += t_pi;
	}

	v_buff = v_t2 + g_ts;

	for (size_t i = t2_i; i < len; ++i) {
		v[i] = v_buff;
		v_buff += g_ts;
	}

	return v_t0;
}

void emf_data_generate(uint8_t *dst, size_t len)
{
	if (len < EMF_PAYLOAD_SIZE) {
		return;
	}

	indenter_speed(speed, EMF_SAMPLE_COUNT);

	for (size_t i = 0; i < EMF_SAMPLE_COUNT; ++i) {
		int32_t noise = (int32_t)(sys_rand32_get() % 201U) - 100;
		float value = (speed[i] * 850.0f) + 100.0f;

		// if (value < 0.0f) {
		// 	value += 4095.0f;
		// }

		int16_t number = (int16_t)value;
		uint16_t encoded = (uint16_t)number;

		dst[i * 2] = encoded & 0xff;
		dst[i * 2 + 1] = encoded >> 8;
	}
}
