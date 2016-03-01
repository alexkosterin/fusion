import sys, os, getopt, csv

z2t = { 0 : 'void',  1 : 'bool',  4 : 'int',  8 : 'double',  -1 : 'string', }

def usage():  ##################################################################
	print >> sys.stderr, """Generate Fusion configuration from csv file. (c) Alex Kosterin 2013-2014
import-csv [--host=...] [--port=...] --profile=... --run --csv=...
  -h, --help       print help and exit.
  -p, --port       fusion port.
  -P, --profile    fusion profile.
  -c, --csv        csv file to importetetate Fu
  -r, --run        run (vs just generating batch file with commands)
  -x, --exec       path to executable [dina.exe]
"""

def main(): ####################################################################
	try:
		opts, args = getopt.getopt(sys.argv[1:], "c:P:p:H:hrx:", ["help", "csv=", "host=", "profile=", "port=", "run", "exec"])
	except getopt.GetoptError as err:
		print >> sys.stderr, str(err)
		usage()
		sys.exit(2)

	opt_csv								= None;
	opt_profile						= None;
	opt_port					    = 3001;
	opt_host    					= '127.0.0.1'
	opt_exec   						= 'dina.exe'
	opt_run    						= False

	## parse arguments ##
	for o, a in opts:
		if o in ("-h", "--help"):
			usage()
			sys.exit()
		elif o in ("-P", "--profile"):
			opt_profile = a
		elif o in ("-p", "--port"):
			opt_port = int(a)
		elif o in ("-H", "--host"):
			opt_host = a
		elif o in ("-c", "--csv"):
			opt_csv = a
		elif o in ("-x", "--exec"):
			opt_exec = a
		elif o in ("-r", "--run"):
			opt_run = True

#	print >> sys.stderr, 'z2t =', z2t
#	print >> sys.stderr, 'opt_csv =', opt_csv, 'opt_port =', opt_port

	for r in csv.DictReader(open(opt_csv, 'rb')):
		z, t, d, ps = int(r['size:']), int(r['type:']), r['data:'], []

		for (p, m) in [(k, r[k]) for k in sorted(r.keys()) if r[k] and k[-1:] != ':']:
			for m0 in m.split(";"):
				ps.append((p, m0.strip()))

#		print >> sys.stderr, z, t, ps

		first = None

		for (p, m) in ps:
			if not first:
				first = (p, m)
				s = "%s --profile=%s --port=%d --host=%s --create%s --%s --msg=%s:%s" % (opt_exec, opt_profile, opt_port, opt_host, "-persist=0" if t else "", z2t.get(z, "size=%d" % z), p, m)
			else:
				s = "%s --profile=%s --port=%d --host=%s --link=%s:%s --target=%s:%s" % (opt_exec, opt_profile, opt_port, opt_host, p, m, first[0], first[1])

			if opt_run:
				os.system(s)
			else:
				print s

		if d:
			s = "%s --profile=%s --port=%d --host=%s --write=%s --%s --msg=%s:%s" % (opt_exec, opt_profile, opt_port, opt_host, d, z2t.get(z, "size=%d" % z), first[0], first[1])

			if opt_run:
				os.system(s)
			else:
				print s

if __name__ == "__main__":
	main()

