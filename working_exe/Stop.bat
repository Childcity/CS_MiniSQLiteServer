@echo off

::Stop server
sc stop CS_MiniSQLiteServerSvc
sc delete CS_MiniSQLiteServerSvc
