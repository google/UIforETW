
UIforETWps.dll: dlldata.obj UIforETW_p.obj UIforETW_i.obj
	link /dll /out:UIforETWps.dll /def:UIforETWps.def /entry:DllMain dlldata.obj UIforETW_p.obj UIforETW_i.obj \
		kernel32.lib rpcns4.lib rpcrt4.lib oleaut32.lib uuid.lib \
.c.obj:
	cl /c /Ox /DREGISTER_PROXY_DLL \
		$<

clean:
	@del UIforETWps.dll
	@del UIforETWps.lib
	@del UIforETWps.exp
	@del dlldata.obj
	@del UIforETW_p.obj
	@del UIforETW_i.obj
