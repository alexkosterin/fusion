import sys, os, getopt, time, mb

def usage():  ##################################################################
	print >> sys.stderr, """
Torture reg/undeg. (c) 2014 Alex Kosterin 
prog [--host=...] [--port=...] [--profile=PROFILE] [--name=NAME] [--nr=N] [--delay=NNN]
"""

_ticked = 0;

def ticked():
	global _ticked
	t = int(time.time())
	#print "%d:%d->%d" % (_ticked, t, _ticked < t)
	if _ticked < t:
		_ticked = t
		return 1
	return 0

def main(): ####################################################################
	try:
		opts, args = getopt.getopt(sys.argv[1:], "hH:P:p:n:N:d:", ["help", "host=", "profile=", "port=", "nr=", "name=", "delay="])
	except getopt.GetoptError as err:
		print >> sys.stderr, str(err)
		usage()
		sys.exit(2)


	opt_profile	= ''
	opt_port   	= '3001'
	opt_host	= '127.0.0.1'
	opt_nr		= 1000000
	opt_delay	= 1
	opt_name	= 'Torture - instantiate. pid=%d' % os.getpid()

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


	t = 0
	for i in xrange(0, opt_nr):
		t += ticked()
		print "\r%02.1f%% %c" % (100.*i/opt_nr, "-/|\\"[t % 4]),
		try:
			c = mb.Client(opt_name + " #" + str(i))
			if opt_delay: time.sleep(opt_delay)
		except:
			pass
		del c

main()
