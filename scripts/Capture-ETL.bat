@echo off
pushd %~dp0
wpr -start Tracing.wprp -filemode

echo Reproduce your issue now, then
pause

wpr -stop QVFR.etl
popd
