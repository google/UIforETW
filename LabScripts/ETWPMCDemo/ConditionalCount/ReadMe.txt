This demonstrates how pre-sorting of an array can dramatically affect the number
of branch mispredicts. The exact behavior is highly dependent on your compiler
and CPU but this can be a good test case for testing CPU performance counters
such as branch mispredict counts.
