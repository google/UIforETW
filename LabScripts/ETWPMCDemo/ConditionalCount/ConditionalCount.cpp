#include <stdlib.h>
#include <string>
#include <algorithm>

int sum_array(unsigned char* p, size_t count);

int main(int argc, char* argv[])
{
	int64_t start = __rdtsc();
	unsigned char buffer[8192];

	for (auto& x : buffer)
	{
		x = (unsigned char)((rand() / 71) & 255);
	}

	if (argc > 1 && strcmp(argv[1], "-sort") == 0)
		std::sort(buffer, buffer + sizeof(buffer));

	int64_t mid = __rdtsc();
	int total = 0;
	for (int i = 0; i < 30000; ++i)
	{
		total += sum_array(buffer, sizeof(buffer));
	}
	int64_t end = __rdtsc();
	printf("%5.2f MCycles for initialization and %5.2f MCycles for conditional adding.\n", (mid - start) / 1e6, (end - mid) / 1e6);

	return 0;
}
