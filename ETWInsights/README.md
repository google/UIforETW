# ETWInsights

## flame_graph

flame_graph is a command-line tool to generate a
[flame graph](http://www.brendangregg.com/flamegraphs.html) from an ETW trace.

Usage: `flame_graph.exe --trace <trace_file_path> [options]`

Options:

- `--process_name`: Only include stacks from processes with the specified name.
- `--tid`: Only include stacks from the specified thread.
- `--start_ts`: Only include stacks that occurred after the specified timestamp.
- `--end_ts`: Only include stacks that occurred before the specified timestamp.
- `--out`: Output file path. Default: <trace_file_path>.flamegraph.txt

Timestamps are a number of microseconds elapsed since the beginning of the
trace.

`flame_graph.exe` produces a text file that tells how much time was spent in
each call stack. To convert this text file to a nice-looking SVG report, use
this [perl script](https://github.com/brendangregg/FlameGraph/blob/master/flamegraph.pl).
