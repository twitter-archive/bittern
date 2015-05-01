#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <string.h>

#include "murmurhash3.h"

#define OPERATIONS (1UL * 1000UL * 1000UL)

int main(int argc, char **argv)
{
	unsigned char buffer[4096];
	struct timeval tv0, tv1;
	int i;
	float elapsed_sec;
	float mbytes;
	uint128_t hash_value;

	memset(buffer, 0, 4096);
	hash_value = murmurhash3_128(buffer, 4096);
	if (uint128_eq(hash_value, MURMURHASH3_128_4K0)) {
		printf("murmurhash3_128(0x00*4096) = MURMURHASH3_128_4K0 = " UINT128_FMT ": ok\n",
		       UINT128_ARG(MURMURHASH3_128_4K0));
	} else {
		printf("murmurhash3_128(0x00*4096) = MURMURHASH3_128_4K0 = " UINT128_FMT ": failed\n",
		       UINT128_ARG(MURMURHASH3_128_4K0));
		exit(1);
	}

	memset(buffer, 0x55, 4096);
	hash_value = murmurhash3_128(buffer, 4096);
	printf("murmurhash3_128(0x55*4096) = " UINT128_FMT "\n",
	       UINT128_ARG(hash_value));

	gettimeofday(&tv0, NULL);
	for (i = 0; i < OPERATIONS; i++)
		hash_value = murmurhash3_128(buffer, 4096);
	gettimeofday(&tv1, NULL);

	tv1.tv_sec -= tv0.tv_sec;
	tv1.tv_usec -= tv0.tv_usec;
	if (tv1.tv_usec < 0) {
		tv1.tv_sec--;
		tv1.tv_usec += 1000000L;
	}

	printf("murmurhash3_test: %lu 4k pages hashed in %ld.%06ld seconds\n",
	       OPERATIONS,
	       tv1.tv_sec,
	       tv1.tv_usec);
	elapsed_sec = (float)tv1.tv_sec + (float)tv1.tv_usec / 1000000.0;
	mbytes = ((float)OPERATIONS * 4096.0) / (1024.0 * 1024.0);
	printf("murmurhash3_test: mbytes/sec = %f\n", mbytes / elapsed_sec);
	printf("murmurhash3_test: single operation latency = %f usecs\n",
	       (elapsed_sec / (float)OPERATIONS) * (1000.0 * 1000.0));

	exit(0);
}
