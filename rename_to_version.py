import os
import sys

prefix = 'const float kCurrentVersion = '

version_header = sys.argv[1]
for line in open(version_header).readlines():
	if line.startswith(prefix):
		version = line[len(prefix) : len(prefix) + 4]
		print 'Renaming zip files to version "%s"' % version
		os.rename('etwpackage.zip', 'etwpackage%s.zip' % version)
		os.rename('etwsymbols.zip', 'etwsymbols%s.zip' % version)
		open('makeandsigncab.bat', 'wt').write(
			'makecab.exe etwpackage%s.zip etwpackage%s.cab\r\n'
			'signtool sign /d "UIforETW Package" /du "https://github.com/google/UIforETW/releases" /n "Bruce Dawson" /tr http://timestamp.digicert.com /td SHA256 /fd SHA256 etwpackage%s.cab\r\n'
			% (version, version, version))
		sys.exit(0)
assert(0)
