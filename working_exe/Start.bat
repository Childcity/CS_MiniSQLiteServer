@echo off

::Start server as service
sc create CS_MiniSQLiteServerSvc binpath=%~dp0CS_MiniSQLiteServer.exe
sc description CS_MiniSQLiteServerSvc "Service for CS_MiniSQLiteServer"
sc start CS_MiniSQLiteServerSvc
pause