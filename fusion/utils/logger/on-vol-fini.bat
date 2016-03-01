: Copytight(c) Alex Kosterin 2014
: Fusion logger "on-vol-fini" file example

:See whats passed
@ECHO %0 %1 %2 %3 %4 %5 %6 %7 %8 %9

:First parameter is just finished log file
@COPY "%1" "c:/prj/tmp"

:If you want window to stay open - use PAUSE
:PAUSE

:If you want window to go away - use EXIT
@EXIT
