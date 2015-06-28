This project creates a DLL that can emit custom ETW events. By using this DLL in your
projects you can make it easier to analyze ETW traces. The emitted ETW events can serve
as signposts or can contain additional context.

As a sample of how to use this DLL see UIforETW and ETWEventDemo, both available at
https://github.com/google/UIforETW.

UIforETW is also the recommended way to record traces that are using this DLL since it
registers the DLL and records ETW traces that listen to its providers.
