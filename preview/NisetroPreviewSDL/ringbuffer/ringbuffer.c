#include "ringbuffer.h"

int32_t ringbuffer_init(ringbuffer *ringbuffer, uint8_t *buffer, uint32_t element_size, uint32_t element_count)
{
	if (!(element_count && !(element_count & (element_count - 1))))
		return -1;

	ringbuffer->tail = 0;
	ringbuffer->head = 0;
	ringbuffer->element_size = element_size;
	ringbuffer->element_count_mask = element_count - 1;
	ringbuffer->buffer = buffer;

	return 0;
}

uint32_t ringbuffer_write(ringbuffer *ringbuffer, uint8_t *data, uint32_t element_count)
{
	uint32_t elements_written = ringbuffer_get_available(ringbuffer);

	if (elements_written > element_count)
		elements_written = element_count;

	if (ringbuffer->tail + elements_written > ringbuffer->element_count_mask)
	{
		uint32_t p1 = (ringbuffer->element_count_mask - ringbuffer->tail + 1);
		uint32_t p2 = (elements_written - p1);

		memcpy(ringbuffer->buffer + (ringbuffer->tail * ringbuffer->element_size), data, p1 * ringbuffer->element_size);
		memcpy(ringbuffer->buffer, data + p1 * ringbuffer->element_size, p2 * ringbuffer->element_size);
	}
	else
	{
		memcpy(ringbuffer->buffer + (ringbuffer->tail * ringbuffer->element_size), data, elements_written * ringbuffer->element_size);
	}

	ringbuffer->tail = ((ringbuffer->tail + elements_written) & ringbuffer->element_count_mask);

	return elements_written;
}

uint32_t ringbuffer_read(ringbuffer *ringbuffer, uint8_t *data, uint32_t element_count)
{
	uint32_t elements_read = ringbuffer_get_remaining(ringbuffer);

	if (elements_read > element_count)
		elements_read = element_count;

	if (ringbuffer->head + elements_read > ringbuffer->element_count_mask)
	{
		uint32_t p1 = (ringbuffer->element_count_mask - ringbuffer->head + 1);
		uint32_t p2 = (elements_read - p1);

		memcpy(data, ringbuffer->buffer + (ringbuffer->head * ringbuffer->element_size), p1 * ringbuffer->element_size);
		memcpy(data + p1 * ringbuffer->element_size, ringbuffer->buffer, p2 * ringbuffer->element_size);
	}
	else
	{
		memcpy(data, ringbuffer->buffer + (ringbuffer->head * ringbuffer->element_size), elements_read * ringbuffer->element_size);
	}

	ringbuffer->head = ((ringbuffer->head + elements_read) & ringbuffer->element_count_mask);

	return elements_read;
}

uint32_t ringbuffer_discard(ringbuffer *ringbuffer, uint32_t element_count)
{
	uint32_t elements_discard = ringbuffer_get_remaining(ringbuffer);

	if (elements_discard > element_count)
		elements_discard = element_count;

	ringbuffer->head = (ringbuffer->head + elements_discard) & ringbuffer->element_count_mask;

	return elements_discard;
}
