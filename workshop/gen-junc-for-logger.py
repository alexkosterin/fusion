from mb import *
import random

l = (	"",
	"Accordingly",
	"all",
	"and",
	"are",
	"available",
	"by",
	"called",
	"Class",
	"Does",
	"effect",
	"for",
	"from",
	"function",
	"generating",
	"getstate",
	"have",
	"if",
	"ignored",
	"jumpahead",
	"methods",
	"no",
	"not",
	"Not",
	"NotImplementedError",
	"numbers",
	"on",
	"operating",
	"provided",
	"raise",
	"random",
	"rely",
	"reproducible",
	"seed",
	"sequences",
	"setstate",
	"software",
	"sources",
	"state",
	"system",
	"systems",
	"that",
	"the",
	"urandom",
	"uses"
)

n, N = 0, 2000000000

def cbi(m, d):
	global c, n
	n += 1
	c.pub(m, d + 1)

def cbs(m, s):
	global c, n
	n += 1
	c.pub(m, random.choice(l) + " " + random.choice(l))

with Client("Str Ex.") as c:
	c.reg(profile='demo')

	mping = c.mopen('ping', 'rw')
	c.sub(mping, cbi, type = int)
	c.pub(mping, 0)

	ms = c.mopen('s', 'rw')
	c.sub(ms, cbs, type = str)
	c.pub(ms, '')

	while n < N:
		if (n % 1000) == 0: print '.',
		c.dispatch(True, 1000)


