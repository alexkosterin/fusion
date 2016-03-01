################################################################################
# NOTE A.K.: need to make link %python%/libs <-> %python%/PCBuild
################################################################################

import os, re
from distutils.core import setup, Extension

ver = "0.0.0.unknown"

if os.environ["FUSION"]:
	prj = os.environ["FUSION"]
else:
	prj = ".."

try:
	vrx = re.compile('GENVER_STRING\s+"v(\d+)\.(\d+)\.(\d+)(\.\*?g?(.+))?"')

	for s in open(prj + "/include/genver.h"):
		m = vrx.search(s)
		if m:
			ver = "%s.%s.%s.%s" % (m.group(1), m.group(2), m.group(3), m.group(5))
			break
except:
	pass

setup(
	name						= "mb",
    version						= ver,
    description					= "Fusion Client library",
	long_description			= "Fusion Client library",
	author						= "Alex Kosterin",
	author_email				= "akosterin",
	platforms					= ["windows"],
	license						= "Copyright (c) 2014 National Oilwell Varco",
	url							= "fusion",
	ext_modules					= [
		Extension(
			"mb", 
			sources				= ["mbpy.cpp", prj + "/common/lock.cpp", prj + "/common/tsc.cpp"], 
			include_dirs		= [prj, os.environ["PROTOBUF"]],
			libraries			= ["mb"],
			library_dirs		= [prj +"/release"],
		)]
)
