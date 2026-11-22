#ifndef GBOYC_HASH_H
#define GBOYC_HASH_H

#include <stddef.h>
#include <stdint.h>

#define FNV1A32_INIT 2166136261u

static inline uint32_t fnv1a32_update(uint32_t hash, const void *data, size_t size) {
	const uint8_t *bytes = data;

	for (size_t i = 0; i < size; i++) {
		hash ^= bytes[i];
		hash *= 16777619u;
	}
	return hash;
}

static inline uint32_t fnv1a32(const void *data, size_t size) {
	return fnv1a32_update(FNV1A32_INIT, data, size);
}

#endif
