#include "bsp_common.h"

/*
 * brief  : bsp_common_gpio_is_valid.
 * input  : io_num is the GPIO index to validate.
 * output : true if the GPIO can be used; otherwise false.
 * type   : public
 * theme  : provide one guard point for optional/unused pins so callers can
 *          skip invalid GPIO operations before driver access.
 */
bool bsp_common_gpio_is_valid(gpio_num_t io_num)
{
	return io_num != GPIO_NUM_NC;
}

/*
 * brief  : bsp_common_read_u16_be.
 * input  : data points to at least 2 bytes in big-endian order.
 * output : decoded 16-bit value; returns 0U when data is NULL.
 * type   : public
 * theme  : centralize big-endian decoding for protocol payload fields to keep
 *          parsing code consistent, compact, and null-safe.
 */
uint16_t bsp_common_read_u16_be(const uint8_t* data)
{
	if (data == NULL) {
		return 0U;
	}
	return (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
}

/*
 * brief  : bsp_common_read_u16_le.
 * input  : data points to at least 2 bytes in little-endian order.
 * output : decoded 16-bit value; returns 0U when data is NULL.
 * type   : public
 * theme  : centralize little-endian decoding for binary data sources that use
 *          LE layout, avoiding duplicated byte-swap logic at call sites.
 */
uint16_t bsp_common_read_u16_le(const uint8_t* data)
{
	if (data == NULL) {
		return 0U;
	}
	return (uint16_t)(((uint16_t)data[1] << 8) | (uint16_t)data[0]);
}
