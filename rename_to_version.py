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
		sys.exit(0)
assert(0)
