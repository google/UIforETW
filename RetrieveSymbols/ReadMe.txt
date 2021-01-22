This tool is designed to download PE files and symbols from symbol servers.
Normally this is done automatically by tools such as windbg but it can be
helpful to be able to download these files on demand.

Sample usage:

> rem Add the VS tools to the path, for access to dumpbin
> "%vs120comntools%vsvars32.bat"

>dumpbin /headers "c:\Program Files (x86)\Google\Chrome\Application\chrome.exe" | findstr "RSDS date image"
        54E3AECF time date stamp Tue Feb 17 13:12:47 2015
          400000 image base (00400000 to 004D2FFF)
            0.00 image version
           D3000 size of image
    54E3AECF cv           5D 0008DF80    8D380    Format: RSDS, {283A66AE-3EF3-4383-8798-F6617112B1F6}, 1, chrome.exe.pdb

> RetrieveSymbols {283A66AE-3EF3-4383-8798-F6617112B1F6}, 1 chrome.exe.pdb
Looking for PDB file 283A66AE3EF343838798F6617112B1F6 1 chrome.exe.pdb.
Found file - placed it in d:\src\symbols\chrome.exe.pdb\283A66AE3EF343838798F6617112B1F61\chrome.exe.pdb.

> RetrieveSymbols 54E3AECF D3000 chrome.exe
Looking for PE file chrome.exe 54e3aecf d3000.
Found file - placed it in d:\src\symbols\chrome.exe\54E3AECFd3000\chrome.exe.

The first invocation of RetrieveSymbols uses the GUID, age, and PDB name from
the RSDS line of the dumpbin output -- the extraneous '{', '}', ',' and '-'
characters are stripped out.

The second invocation of RetrieveSymbols uses the time date stamp from the
first line of the dumpbin output, the "size of image" data, and the
executable name.

This information can also be obtained from breakpad reports, from windbg
by using "lmv m chrome_elf" and "!lmi chrome_elf.dll", from ETW traces,
and from other sources.

This tool is used by StripChromeSymbols.py to retrieve and then strip
Chrome's symbols in order to avoid performance problems in WPA:
https://randomascii.wordpress.com/2014/11/04/slow-symbol-loading-in-microsofts-profiler-take-two/
