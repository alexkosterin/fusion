Fusion python module

Environment vars used:
PYTHON			=> points to python executable
PYTHON_INCLUDE	=> $(python-dist)/include
PYTHON_LIB		=> $(python-dist)/lib

python 2 on my system :

PYTHON			=> C:/prj/Python-2.7.5/PCbuild/python.exe
PYTHON_INCLUDE	=> C:/prj/Python-2.7.5/include
PYTHON_LIB		=> C:/prj/Python-2.7.5/lib

python 3 on my system :

PYTHON			=> C:/prj/Python-3.4.2/PCbuild/python.exe
PYTHON_INCLUDE	=> C:/prj/Python-3.4.2/include
PYTHON_LIB		=> C:/prj/Python-3.4.2/lib

Example:

from mb import *

n, N = 0, 1000000

def cb(m, d):
	global n, c
	n = d
	c.pub(m, d + 1)

with Client('example') as c:
	c.reg(profile = 'demo')
	m = c.open('ping', 'rw')
	c.sub(m, cb, adaptor=int)
	c.pub(m, 0)
	while n < N:
		c.dispatch(True)

