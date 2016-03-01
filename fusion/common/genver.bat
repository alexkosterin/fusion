:This file generates version string using git

@IF %1==rungit goto RUNGIT

:do we have git?
@git --version > NUL
@IF ERRORLEVEL 1 GOTO NO_GIT

ECHO // Do not edit! This file was created automatically by build. > $$$

:HAVE_GIT
ECHO // Version generated using command: git describe --tags --match v* --dirty=-* >> $$$

FOR /F "delims=- tokens=1,2,3,4 usebackq" %%i in (`@%0 rungit`) do @(
 ECHO #define GENVER_STRING "%%i.%%j.%%l%%k">> $$$
 ECHO #define GENVER_BUILD   %%j>> $$$
)

:: check file diff
@fc $$$ %1 > NUL

@IF "%errorlevel%" NEQ "0" (
   @MOVE /Y $$$ %1
) ELSE (
   @DEL $$$
)

@EXIT 0

:NO_GIT
ECHO // No git found... >>  %1
ECHO #define _STRINGIFY(S)	#S
ECHO #define STRINGIFY(S)	_STRINGIFY(S)
ECHO #define GENVER_STRING "v" STRINGIFY(VER_MAJ) "." STRINGIFY(VER_MIN) ".UNKNOWN">> %1
ECHO #define GENVER_BUILD  0 >> %1
@EXIT 0

:RUNGIT
@git describe --tags --match v* --dirty=-*
