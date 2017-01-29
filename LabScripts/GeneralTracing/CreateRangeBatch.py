import sys

marks_csv_name = sys.argv[1]
lines = open(marks_csv_name).readlines()[1:]
assert(len(lines) == 2)
parts0 = lines[0].strip().split(',')
assert(parts0[0] == 'Starting')
assert(parts0[1] == '1')
parts1 = lines[1].strip().split(',')
assert(parts1[0] == 'Stopping')
assert(parts1[1] == '1')

print "set RANGE=-range %ss %ss" % (parts0[2], parts1[2])
