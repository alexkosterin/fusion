: Fusion
: Copytight(c) National Alex Kosterin

: Example: how to copy log files to TEMP directory

: First parameter is just finished log file
@COPY "%1" %%TEMP%%

: If you want window to stay open - use PAUSE
:@PAUSE

: If you want window to go away - use EXIT
@EXIT
