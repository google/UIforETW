/*
This exists as a separate source file, and Link Time Code Generation is
disabled, in order to hide information from the optimizer so that it won't
realize that this function is pure and idempotent.
*/

//#define	UNWOUND

int sum_array(unsigned char* p, size_t count)
{
	int result = 0;
#ifdef UNWOUND
	// I expected this to have a higher percentage of mispredicted
	// branches because four out of five branches in the loop are
	// unpredictable (the loop-end branch is very predictable).
	// But this code somehow shows almost zero branch mispredicts,
	// and performance that is equivalent to the perfectly predicted
	// code. Very odd.
	for (size_t i = 0; i < count; i += 4)
	{
		if (p[i + 0] < 128)
			result += p[i + 0];
		if (p[i + 1] < 128)
			result += p[i + 1];
		if (p[i + 2] < 128)
			result += p[i + 2];
		if (p[i + 3] < 128)
			result += p[i + 3];
	}
#else
for (size_t i = 0; i < count; i += 1)
{
	// This test relies on this being implemented using a conditional
	// branch instruction.
	if (p[i] < 128)
		result += p[i];
}
#endif
	return result;
}
