import sys, os, getopt, string, datetime, mb, time, threading

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
  -t  --timer=N    Using timer, N is in secs
"""

try:
	opts, args = getopt.getopt(sys.argv[1:], "hf:p:P:n:t:", ["help", "fusion=", "port=", "profile=", "ping=", "pong=", "timer="])
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
opt_timer	= 60
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
	elif o in ("-t", "--timer"):
		opt_timer = int(a)

input	= 0
output	= 0

def cb(m, v):
	global opt_nr, input, output
	input += 1
	print "cb: v = %d input = %d output = %d" % (v, input, output)
	if input <= opt_nr:
		for i in xrange(0, input):
			client.post(m, mb.CID_ALL|mb.CID_NOSELF, i)
			output += 1

def work(c):
	global opt_nr, input, output
	print c
#	while input <= opt_nr:
#		print "work: input = %d output = %d" % (input, output)
#		c.dispatch(False, 1000)
#		time.sleep(opt_timer)
	print "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ input = %d output = %d" % (input, output)

with mb.Client("ping" if bool(opt_ping) else "pong") as c:
	client = c

	c.reg(opt_profile)
	m = c.mopen(opt_ping if bool(opt_ping) else opt_pong, "rw")
	c.sub(m, cb, type=int)

	if bool(opt_ping):
		c.post(m, mb.CID_ALL|mb.CID_NOSELF, 0)

	t = threading.Timer(opt_timer, work, [client])
	t.start()

	while input <= opt_nr:
		c.dispatch(True, 0)
		time.sleep(10)

	t.cancel()

print ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> input = %d output = %d" % (input, output)
