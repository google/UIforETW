This project creates a DLL that can emit custom ETW events. By using this DLL in your
projects you can make it easier to analyze ETW traces. The emitted ETW events can serve
as signposts or can contain additional context.

As a sample of how to use this DLL see UIforETW, also available at https://github.com/randomascii/main.

UIforETW is also the recommended way to record traces that are using this DLL since it
registers the DLL and records ETW traces that listen to its providers.
