import sys, os, getopt, string, datetime, mb, time, threading

def usage():  ##################################################################
	print >> sys.stderr, """Ping-pong fusion python example. NOV(c) 2014
pingpong ...
  -h, --help       Print help and exit.
  -H, --host       Fusion address.
  -p, --port       Fusion port.
  -P, --profile    Profile to use.
"""

try:
	opts, args = getopt.getopt(sys.argv[1:], "hH:p:P:t:m:t:a:", ["help", "host=", "port=", "profile=", "name=", "input=", "output=", "period=", "amp="])
except getopt.GetoptError as err:
	print >> sys.stderr, str(err)
	usage()
	sys.exit(1)

opt_name		= ''
opt_host		= 'localhost'
opt_port		= '3001'
opt_profile		= ''
opt_name		= ''
opt_input		= None
opt_output		= None
opt_period		= 10
opt_amplitude	= 100

d = None
a = None

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
	elif o in ("--input"):
		opt_input = a
	elif o in ("--output"):
		opt_output = a
	elif o in ("-t", "--period"):
		opt_period = int(a)
	elif o in ("-a", "--amp"):
		opt_amplitude = int(a)

def callback(m, v):
	global timer, d, a
	print "m=%r v=%r"%(m, v)
	timer.cancel()
	d = v + opt_amplitude / 2
	a = -opt_amplitude
	ontimer()

def ontimer():
#	global timer, client, output, d, a
	global timer, d, a
	d, a = d + a, -a
	print "d=%r a=%r"%(d, a)
	client.pub(output, d)
	timer = threading.Timer(opt_period, ontimer)
	timer.start()

with mb.Client(opt_name) as client:
	client.reg(opt_profile, host=opt_host, port=opt_port)
	input = client.mopen(opt_input, "r")
	client.sub(input, callback, type=float)
	output = client.mopen(opt_output, "w")
	timer = threading.Timer(opt_period, ontimer, [client])

	while True:
		client.dispatch(True, 1000)


