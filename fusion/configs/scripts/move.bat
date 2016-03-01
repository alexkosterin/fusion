: Fusion
: Copytight(c) alex kosterin

: Example: how to move log files to TEMP directory

: First parameter is just finished log file
@MOVE "%1" %%TEMP%%

: If you want window to stay open - use PAUSE
:@PAUSE

: If you want window to go away - use EXIT
@EXIT
