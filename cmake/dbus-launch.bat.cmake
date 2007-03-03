:: environment setting for dbus clients
@echo off

:: session bus address
set DBUS_SESSION_BUS_ADDRESS=@DBUS_SESSION_BUS_DEFAULT_ADDRESS@

:: system bus address
set DBUS_SYSTEM_BUS_DEFAULT_ADDRESS=@DBUS_SYSTEM_BUS_DEFAULT_ADDRESS@ 

if exist bus\session.conf (
  @echo starting local dbus daemon
	 start "D-Bus daemon" "bin\dbus-daemon" "--config-file=bus\session.conf"
) else (
	if not "%DBUSDIR%"=="" (
	  @echo starting dbus daemon identified by DBUSDIR=%DBUSDIR%
		start "D-Bus daemon" "%DBUSDIR%\bin\dbus-daemon" "--session"
		pause
	) else (
		if exist "%ProgramFiles%\dbus\bin\dbus-daemon.exe" (
		  @echo starting global dbus daemon located in %ProgramFiles%\dbus
			start "D-Bus daemon" "%ProgramFiles%\dbus\bin\dbus-daemon" "--session"
		) else (
	  	@echo please set DBUSDIR to your DBUS installation dir and restart this script
		) 
	)
)
