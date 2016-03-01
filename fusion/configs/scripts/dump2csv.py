#!/bin/python

import sys, os, re, getopt, string

prx = re.compile(r"^([^=]+)=(.*)$")
atr = re.compile(r"^([_a-zA-Z][_0-9a-zA-Z]*:)(.*)$")
pth = re.compile(r"^(.*)[\\/]([^\\/]+)$")

def usage():  ##################################################################
	print >> sys.stderr, """Tabulate Fusion links. (c) Alex Kosterin 2013-2014
    tablink [-o output] [-r new=old[,...]] [-x one[,...]] [input-file]
    -h, --help      Print help and exit.
    -o, --output    Output file
    -r, --rename    Rename header attribute/path/profile
    -x, --exclude   Exclude attribute/path/profile list
"""

def ordered(ord, input): #######################################################
	lst = []

	for s in ord:
		if s in input:
			lst.append(s)

	for s in input:
		if not s in ord:
			lst.append(s)

	return lst

def main(): ####################################################################
	try:
		opts, args = getopt.getopt(sys.argv[1:], "o:r:x:h", ["help", "exclude=", "order=", "output=", "rename="])
	except getopt.GetoptError, err:
		print str(err)
		usage()
		sys.exit(2)

	xlst		= []
	olst		= []
	lines		= []
	pfxs		= {}
	rrename	= {}	# reverse rename

	output	= sys.stdout

	# parse arguments
	for o, a in opts:
		if o in ("-h", "--help"):
			usage()
			sys.exit()
		elif o in ("-o", "--output"):
			output = open(a, 'w')
		elif o in ("-r", "--rename"):
			for aa in a.split(','):
				m = prx.match(aa)
				if m:
					rrename[m.group(2)] = m.group(1)
				else:
					print >> sys.stderr, "Bad rename syntax: '%s'. Expect something like: 'a=b'." % aa
					exit(1)
		elif o in ("-x", "--exclude"):
			for aa in a.split(','):
				xlst.append(aa)
		elif o in ("--order"):
			for aa in a.split(','):
				olst.append(aa)

	if len(args) == 0:
		input	= sys.stdin
	elif len(args) == 1:
		input = open(args[0], 'r')
	else:
		print >> sys.stderr, "Too many inputs: '%s'. Expect at most one." % args
		exit(1)

	# process input
	for l in input:
		d = {}
		for s in string.split(l, "\t"):
			s = s.rstrip('\r\n')
			m = atr.match(s)
			if m:
				# attribute
				p, v = m.group(1), m.group(2)
			else:
				# tag
				m = pth.match(s)
				if m:
					p, v = m.group(1), m.group(2)
				else:
					print >> sys.stderr, "ignore: '%s'" % s
					continue

			p = p.replace("\\", "/");

			if d.has_key(p):
				d[p] = d[p] + "; " + v
			else:
				d[p] = v

			pfxs[p] = None

		lines.append(d)

	lst = [p for p in ordered(olst, pfxs.keys()) if not p in xlst]

	# print profiles header
	print >> output, ",".join([rrename.get(p, p) for p in lst])

	# print links
	for d in lines:
		print >> output, ",".join([d.get(p, "") for p in lst])

if __name__ == "__main__":
	main()

