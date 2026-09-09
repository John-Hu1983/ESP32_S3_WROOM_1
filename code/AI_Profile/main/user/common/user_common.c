#include "user_common.h"

/*
 * brief : user_common_gpio_is_valid.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
bool user_common_gpio_is_valid(gpio_num_t io_num)
{
	return io_num != GPIO_NUM_NC;
}

/*
 * brief : user_common_read_u16_be.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
uint16_t user_common_read_u16_be(const uint8_t* data)
{
	if (data == NULL) {
		return 0U;
	}
	return (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
}

/*
 * brief : user_common_read_u16_le.
 * input : see parameters.
 * output: return value from this function.
 * type  : public
 */
uint16_t user_common_read_u16_le(const uint8_t* data)
{
	if (data == NULL) {
		return 0U;
	}
	return (uint16_t)(((uint16_t)data[1] << 8) | (uint16_t)data[0]);
}
