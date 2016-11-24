/*
Copyright 2016 Google Inc. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

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
