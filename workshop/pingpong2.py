import sys, os, getopt, string, datetime
from mb import *

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
	opts, args = getopt.getopt(sys.argv[1:], "hf:p:P:n:m:x", ["help", "fusion=", "port=", "profile=", "ping=", "pong=", "exclusive="])
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
opt_excl	= False

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
	elif o in ["-x", "--exclusive"]:
		opt_excl = not opt_excl

if not (bool(opt_ping) ^ bool(opt_pong)):
	print >> sys.stderr, "Error: Either ping or pong must be given"
	exit(1)

n = 1

def cb(m, v):
	global n
	client.post(m, CID_ALL|CID_NOSELF, n)
#	print 'v =', v,
	n = n + 1

with Client("ping" if bool(opt_ping) else "pong") as c:
	client = c

	c.reg(opt_profile)
	print 'id =', c.id()
	m = c.mopen(opt_ping if bool(opt_ping) else opt_pong, "xrw" if opt_excl else "rw")
	c.sub(m, cb, type=str)

	if not bool(opt_pong):
		c.post(m, CID_ALL|CID_NOSELF, 0)

	while n <= opt_nr:
		if (n % 1000) == 0: print '.',
		c.dispatch(True, 1000)

