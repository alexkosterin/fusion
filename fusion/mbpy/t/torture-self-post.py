import sys, os, getopt, time
import mb, progress

PROG = 'Fusion Test Suite: self post(s)'
VER  = '0.0'

def usage():  ##################################################################
  print >> sys.stderr, """
%s v%s
Syntax:
%s [--host=HOST] [--port=PORT] [--profile=PROFILE] [--name=NAME] [--nr=MAX] [--delay=SECS] [[--[a]sync] [--msg=MSG]]...
""" % (PROG, VER, sys.argv[0])

client = None
n = 0

def cb(m, i):
  global n
  n += 1
  client.post(m, client.id(), i + 1)

def main(): ####################################################################
  try:
    opts, args = getopt.getopt(sys.argv[1:], "hH:P:p:n:N:d:m:", [
      "help", "host=", "profile=", "port=", "nr=", "name=", "delay=", "sync", "async", "msg="])
  except getopt.GetoptError as err:
    print >> sys.stderr, str(err)
    usage()
    sys.exit(2)

  opt_profile = ''
  opt_port    = '3001'
  opt_host    = '127.0.0.1'
  opt_nr      = 1000000
  opt_delay   = .5
  opt_name    = "%s [%d]" % (PROG, os.getpid())
  opt_msgs    = []
  opt_async   = False

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
      opt_nr = int(a)
    elif o in ("-d", "--delay"):
      opt_delay = float(a)
    elif o in ("-m", "--msg"):
      opt_msgs.append({'async': opt_async, 'name': a})
    elif o in ("-s", "--sync"):
      opt_async = False
    elif o in ("-a", "--async"):
      opt_async = True

  if not opt_msgs:
    print 'Nothing to do...'
    sys.exit(0)

  have_sync = not reduce(lambda v, msg: v and msg['async'], opt_msgs, True)

  global client

  client = mb.Client(opt_name)
  client.reg(opt_profile)

  # open/subscribe
  for msg in opt_msgs:
    msg['mid'] = client.open(msg['name'], 'rw')
    client.sub(msg['mid'], cb, adaptor=int, async=msg['async'])

  # seed traffic...
  for msg in opt_msgs:
    client.post(msg['mid'], client.id(), 0)

  N = len(opt_msgs) * opt_nr
  p = progress.Progress(N)

  # working cycle/indication
  while n < N:
    print p.indicator(n),

    if have_sync:
      time.sleep(opt_delay)
      client.dispatch(True, 1)

  # close
  for msg in opt_msgs:
    client.close(msg['mid'])


  print p.indicator(n)

  client.unreg()

main()
