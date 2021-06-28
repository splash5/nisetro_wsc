#ifndef __RING_BUFFER_H__
#define __RING_BUFFER_H__

#include <memory.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

	typedef struct
	{
		uint32_t tail;
		uint32_t head;

		uint8_t *buffer;
		uint32_t element_size;
		uint32_t element_count_mask;

	} ringbuffer;

	int32_t ringbuffer_init(ringbuffer *ringbuffer, uint8_t *buffer, uint32_t element_size, uint32_t element_count);
	
	__inline uint32_t ringbuffer_is_empty(ringbuffer *ringbuffer)
	{
		return (ringbuffer->head == ringbuffer->tail);
	}

	__inline void ringbuffer_clear(ringbuffer *ringbuffer)
	{
		ringbuffer->head = 0;
		ringbuffer->tail = 0;
	}

	__inline uint32_t ringbuffer_get_remaining(ringbuffer *ringbuffer)
	{
		if (ringbuffer->tail >= ringbuffer->head)
			return ringbuffer->tail - ringbuffer->head;

		return ringbuffer->element_count_mask - (ringbuffer->head - ringbuffer->tail) + 1;
	}

	__inline uint32_t ringbuffer_get_available(ringbuffer *ringbuffer)
	{
		return (ringbuffer->element_count_mask - ringbuffer_get_remaining(ringbuffer));
	}

	uint32_t ringbuffer_write(ringbuffer *ringbuffer, uint8_t *data, uint32_t element_count);
	uint32_t ringbuffer_read(ringbuffer *ringbuffer, uint8_t *data, uint32_t element_count);
	uint32_t ringbuffer_discard(ringbuffer *ringbuffer, uint32_t element_count);

	void ringbuffer_clear(ringbuffer *ringbuffer);

#ifdef __cplusplus
}
#endif
#endif
