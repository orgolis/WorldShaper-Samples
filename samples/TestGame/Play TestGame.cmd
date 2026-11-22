@echo off
setlocal
set "PATH=C:\Strawberry\c\bin;%PATH%"
rem The newest GameWorldshaper the Hub has installed (0.8.57 or later for the
rem Leviathan's animation hooks; 0.8.55 and 0.8.56 still run it, cues by tick).
set "ENGINES=%LOCALAPPDATA%\GameWorldshaper\Engines"
set "TESTGAME_ENGINE="
for /f "delims=" %%v in ('dir /b /ad /o:n "%ENGINES%" 2^>nul') do if exist "%ENGINES%\%%v\editor.exe" set "TESTGAME_ENGINE=%ENGINES%\%%v\editor.exe"
if not defined TESTGAME_ENGINE (
  echo No GameWorldshaper engine was found in %ENGINES% - install one with the Hub.
  pause
  exit /b 1
)
"%TESTGAME_ENGINE%" --game --project "%~dp0project.schizo"
