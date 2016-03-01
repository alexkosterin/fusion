import sys, os, getopt, string, datetime, mb

def usage():  ##################################################################
	print >> sys.stderr, """Ping-pong fusion python example. NOV(c) 2014
pingpong ...
  -h, --help       Print help and exit.
  -H, --host       Fusion address.
  -p, --port       Fusion port.
  -P, --profile    Profile to use.
  -n               Number of bounces
      --ping=MSG   Play ping role, use message MSG
      --pong=MSG   Play pong role, use message MSG
"""

try:
	opts, args = getopt.getopt(sys.argv[1:], "hf:p:P:n:m:", ["help", "fusion=", "port=", "profile=", "ping=", "pong="])
except getopt.GetoptError as err:
	print >> sys.stderr, str(err)
	usage()
	sys.exit(1)

opt_fusion	= 'localhost';
opt_port	= 3001;
opt_profile	= ''
opt_nr		= 1000
opt_ping	= None
opt_pong	= None
name		= None;

## parse arguments ##
for o, a in opts:
	if o in ("-h", "--help"):
		usage()
		sys.exit(0)
	elif o in ("-H", "--host"):
		opt_fusion = a
	elif o in ("-p", "--port"):
		opt_port = a
	elif o in ("-f", "--profile"):
		opt_profile = a
	elif o in ("-n"):
		opt_nr = int(a)
	elif o in ("--ping"):
		opt_ping = a
	elif o in ("--pong"):
		opt_pong = a

n = 1

def cb(m, v):
	global n
	client.post(m, client.get_mcb()['src'], v + 1)
	n = n + 1

with mb.Client("ping" if bool(opt_ping) else "pong") as c:
	client = c

	c.reg(opt_profile)
	m = c.mopen(opt_ping if bool(opt_ping) else opt_pong, "rw")
	c.sub(m, cb, type=int)

	if not bool(opt_pong):
		c.post(m, mb.CID_ALL|mb.CID_NOSELF, 0)

	while n <= opt_nr:
		if (n % 1000) == 0: print '.',
		c.dispatch(True, 1000)

