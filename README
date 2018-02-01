UIforETW is a user interface for recording ETW (Event Tracing for
Windows) traces, which allow amazingly deep investigations of
performance problems on Windows. Its goals include:
 - making recording ETW traces easy for non-developers
 - making it easy to record additional contextual data such as user
input and CPU temperature in order to make trace analysis easier
 - making trace management easier for developers
 - working around bugs in WPT (Windows Performance Toolkit)

Tutorials on how to use ETW to investigate performance problems on
Windows can be found here:
https://tinyurl.com/etwcentral

For specific details on this project see this post which includes
some documentation and an explanation for why this project was
created:
https://randomascii.wordpress.com/2015/04/14/uiforetw-windows-performance-made-easier/

UIforETW makes it much easier to control how traces are recorded than
using batch files or Microsoft's wprui. UIforETW also works around
numerous problems with ETW tracing (fixing symbol loading issues)
and adds extra features such as categorizing chrome processes,
monitoring working sets, etc.

UIforETW also lists all the recorded traces and displays editable notes
associated with each one.

UIforETW has some features that are specific to Chrome developers - but
it is fully functional for non-Chrome developers as well.

When adding TraceLogging and EventSource providers, UIforETW supports
the same *Name naming convention as other ETW tools. See
https://blogs.msdn.microsoft.com/dcook/2015/09/08/etw-provider-names-and-guids/
to ensure that your provider name and GUID conform to the convention.
EventSource providers should automatically match.

If you want to use UIforETW then you should download the latest
etwpackage.zip from the releases page at:
https://github.com/google/UIforETW/releases

If you want to build or modify UIforETW then you should clone the repo
from https://github.com/google/UIforETW.git and then build
UIforETW\UIforETW.sln.

Pull requests are welcomed. For information on contributing see the
CONTRIBUTING file. When writing commit messages try following the
general guidelines from here:
http://chris.beams.io/posts/git-commit/
Small pull requests are preferred - it's better to do several small
pull requests, each with a unifying theme - than to do one huge pull
request.

This is not an official Google project and is not supported by Google
in any way.
