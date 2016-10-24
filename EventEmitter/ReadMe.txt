This tool can sit in the background, monitoring system activity, and
emit events of the type that etwrecord.bat listens for.

If you use UIforETW.exe then this is unneeded because it both emits
events and controls the tracing which listens for them. However in a
lab setting there will be batch files to start and stop tracing, so
the emitting of events must be done separately.

To start emitting events:
    bin\EventEmitter.exe

To stop emitting events:
    bin\EventEmitter.exe -kill

In order to be more robust it is recommend that a kill command be
executed prior to starting event emitting, to ensure that any
EventEmitter binaries remaining from previous runs are killed.

Alternately, EventEmitter can be left running continuously instead of being
started and stopped for each test.

To-do:
  Stop including afxmt.h in order to avoid the bloat which comes with it
  Add command-line options for controlling what events are emitted - see the
"Configuration stuff" comment in EventEmitter.cpp.
