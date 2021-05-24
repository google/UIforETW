import sys
import os
import zipfile

if len(sys.argv) < 3:
  print('Usage: %s zipfile directory' % sys.argv[0])
  sys.exit(0)

filename = sys.argv[1]
dirToZip = sys.argv[2]

# Assume that dirToZip is a relative directory.
dirToZip = dirToZip

# Request compression
zf = zipfile.ZipFile(filename, "w", zipfile.ZIP_DEFLATED)
for dirname, subdirs, files in os.walk(dirToZip):
  zf.write(dirname)
  for filename in files:
    filePath = os.path.join(dirname, filename)
    zf.write(filePath)
zf.close()
