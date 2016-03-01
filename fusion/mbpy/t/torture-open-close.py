import sys, os, getopt, time, mb

def usage():  ##################################################################
	print >> sys.stderr, """
Torture reg/undeg. (c) Alex Kosterin 2014
prog [--host=...] [--port=...] [--profile=PROFILE] [--name=NAME] [--nr=N] [--delay=NNN] [--flags] --msg=MSG
"""

_ticked = 0
m	= 0
nnr	= 0

def ticked():
	global _ticked
	t = int(time.time())
	#print "%d:%d->%d" % (_ticked, t, _ticked < t)
	if _ticked < t:
		_ticked = t
		return 1
	return 0

def on_open_notify(nm, d):
	global nnr
	assert nm != m
	nnr += 1
		

def main(): ####################################################################
	try:
		opts, args = getopt.getopt(sys.argv[1:], "hH:P:p:n:N:d:f:m:", ["help", "host=", "profile=", "port=", "nr=", "name=", "delay=", "flags=", "msg="])
	except getopt.GetoptError as err:
		print >> sys.stderr, str(err)
		usage()
		sys.exit(2)


	opt_profile	= ''
	opt_port   	= '3001'
	opt_host	= '127.0.0.1'
	opt_nr		= 1000000
	opt_delay	= 1
	opt_name	= 'Torture - mopen/close. pid=%d' % os.getpid()
	opt_flafs	= 'r'
	opt_msg		= None

	## parse arguments ##
	for o, a in opts:
		if o in ("-h", "--help"):
			usage()
			sys.exit()
		elif o in ("-P", "--profile"):
			opt_profile = a
		elif o in ("-p", "--port"):
			opt_port = a
		elif o in ("-H", "--host"):
			opt_host = a
		elif o in ("-n", "--name"):
			opt_name = a
		elif o in ("-N", "--nr"):
			opt_nr = int(a
)
		elif o in ("-d", "--delay"):
			opt_delay = int(a)

		elif o in ("-f", "--flags"):
			opt_flags = a
		elif o in ("-m", "--msg"):
			opt_msg = a

	c = mb.Client(opt_name)
	c.reg(host=opt_host, port=opt_port, profile=opt_profile)
	c.sub(mb.MD_SYS_NOTIFY_OPEN, on_open_notify)
	t = 0
	for i in xrange(0, opt_nr):
		t += ticked()
		print "\r%02.1f%% %c" % (100.*i/opt_nr, "-/|\\"[t % 4]),
		try:
			m = c.open(opt_msg, opt_flags)
			if opt_delay: time.sleep(opt_delay)
			c.close(m)
		except Exception as e:
			print e
		c.dispatch(True, 1)

	print "\r%02.1f%% %c" % (100.*(i+1)/opt_nr, "-/|\\"[t % 4])

	c.dispatch(True, 1)
	c.unsub(mb.MD_SYS_NOTIFY_OPEN)
	c.unreg()
	
	print "open notifications number =", nnr

main()
