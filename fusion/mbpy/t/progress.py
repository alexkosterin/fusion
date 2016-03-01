import time

class Progress(object):
	def __init__(self, n, fmt="-\|/", hz = 2):
		self.max = n
		self.fmt = fmt
		self.hz = hz
		self.time = 0
		self.phase = -1

	def indicator(self, n):
		_t = int(time.time() * self.hz)
		if self.time < _t:
			self.time = _t
			self.phase = (self.phase + 1) % len(self.fmt)
		return "\r%02.1f%% %c" % (100. * n / self.max, self.fmt[self.phase])

if __name__ == "__main__":
	p = Progress(99)
	for i in xrange(0, 100):
		print p.indicator(i),
		time.sleep(0.02)
	print p.indicator(i),
