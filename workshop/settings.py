import sys, os, getopt, string, datetime, mb

def usage():  ##################################################################
	print >> sys.stderr, """Ping-pong fusion python example. NOV(c) 2014
pingpong ...
  -h, --help       Print help and exit.
  -H, --host       Fusion address.
  -p, --port       Fusion port.
  -P, --profile    Profile to use.
  -s  --settings=MSG
"""

try:
	opts, args = getopt.getopt(sys.argv[1:], "hf:p:P:n:s:", ["help", "fusion=", "port=", "profile=", "stettings=", "name"])
except getopt.GetoptError as err:
	print >> sys.stderr, str(err)
	usage()
	sys.exit(2)

opt_fusion	= 'localhost';
opt_port	= 3001;
opt_profile	= ''
opt_nr		= 1000
opt_ping	= None
opt_name    = ''
opt_settings= 'settings'

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
	elif o in ("-s", "--name"):
		opt_name = a
	elif o in ("-s", "--settings"):
		opt_settings = a

txt = None

def cb(m, v):
	global txt
	try:
		print "%sonfiguring = %s" % ("C" if txt is None else "Rec", v)
		txt = v
	except:
		raise

c = mb.Client(opt_name)
c.reg(opt_profile)
m = c.mopen(opt_settings, "r")
c.sub(m, cb, type=str)
c.dispatch(True, 0)

print "Work..."

while True:
	c.dispatch(True, 1000)

