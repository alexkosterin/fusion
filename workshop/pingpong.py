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
  -x  --exclusive  Open messages exclusevily
"""

try:
	opts, args = getopt.getopt(sys.argv[1:], "hf:p:P:n:m:x", ["help", "fusion=", "port=", "profile=", "ping=", "pong=", "exclusive"])
except getopt.GetoptError as err:
	print >> sys.stderr, str(err)
	usage()
	sys.exit(2)

opt_fusion	= 'localhost';
opt_port	= 3001;
opt_profile	= ''
opt_nr		= 1000
opt_ping	= None
opt_pong	= None
opt_excl	= False
first       = True;

## parse arguments ##
for o, a in opts:
	if o in ("-h", "--help"):
		usage()
		sys.exit()
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
		first    = bool(opt_ping) and not bool(opt_pong)
	elif o in ("--pong"):
		opt_pong = a
	elif o in ("-x", "--exclusive"):
		opt_excl = True
	else:
		print >> sys.stderr, "Unknown option:", 
		exit(1)

if not (bool(opt_ping) and bool(opt_pong)):
	print >> sys.stderr, "Error: Both ping and pong must be given"
	exit(1)

c = mb.Client("ping" if first else "pong")
c.reg(opt_profile)
	
print "opening %s as '%s'" % (opt_pong if first else opt_ping, "xw" if opt_excl else "w")
print "opening %s as '%s'" % (opt_ping if first else opt_pong, "xr" if opt_excl else "r")

o = c.mopen(opt_pong if first else opt_ping, "xw" if opt_excl else "w")
i = c.mopen(opt_ping if first else opt_pong, "xr" if opt_excl else "r")

n = 1

def cb(m, v):
	global n
	n = n + 1
#	print "v =", v, "n =", n,
	c.pub(o, v + 1)

c.sub(i, cb, type=int)

if first:
	c.pub(o, 0)

while n <= opt_nr:
	if (n % 1000) == 0: print '.',
	c.dispatch(True, 1000)
