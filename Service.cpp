#include "main.h"
#include "service.h"

static SERVICE_STATUS ServiceStatus;
static SERVICE_STATUS_HANDLE hStatus;
static LPSTR serviceName;
static int service_argc = 0;
static char **service_argv = NULL;

int service_register(DWORD argc, LPTSTR argv[], const LPTSTR serviceName_)
{
	serviceName = serviceName_;
    SERVICE_TABLE_ENTRY ServiceTable[] = {
        { serviceName, (LPSERVICE_MAIN_FUNCTION)service_main},
        {NULL, NULL}
    };

	/*
	* Save argc & argv as service_main is called with different
	* arguments, which are passed from "start" command, not
	* from the program command line.
	* We don't need this behaviour.
	*
	* Note that if StartServiceCtrlDispatcher() succeedes
	* it does not return until the service is stopped,
	* so we should copy all arguments first and then
	* handle the failure.
	*/
	if ((! service_argc) && (! service_argv)) {
		service_argc = argc;
		service_argv = cpy_cstr_list(static_cast<size_t>(argc), argv);
	}

    int ret = StartServiceCtrlDispatcher(ServiceTable);

	if (service_argc && service_argv) {
		for (DWORD i = 0; i < argc; i++) {
			delete[] service_argv[i];
		}
		delete service_argv;
	}

	VLOG(1) << "DEBUG: StartServiceCtrlDispatcher returned with: " << ret;
    return ret;
}

void service_main(DWORD argc, LPTSTR argv[])
{
    ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS; 
    ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    ServiceStatus.dwWin32ExitCode = 0;
    ServiceStatus.dwServiceSpecificExitCode = 0;
    ServiceStatus.dwCheckPoint = 1;
    ServiceStatus.dwWaitHint = 0;

    hStatus = RegisterServiceCtrlHandler(
		serviceName,
        (LPHANDLER_FUNCTION)service_controlhandler);

    if (hStatus == (SERVICE_STATUS_HANDLE)0)
    {
        //Registering Control Handler failed"
		VLOG(1) << "DEBUG: Registering Control Handler failed with: " << hStatus;
        return;
    }

    SetServiceStatus(hStatus, &ServiceStatus);

    // Calling main with saved argc & argv
	VLOG(1) << "DEBUG: starting server from service.";
    ServiceStatus.dwWin32ExitCode = main(service_argc, service_argv);
    ServiceStatus.dwCurrentState  = SERVICE_STOPPED;
    SetServiceStatus(hStatus, &ServiceStatus);
    return;
}

// Control handler function
void service_controlhandler(DWORD request)
{
    switch(request)
    {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            ServiceStatus.dwWin32ExitCode = 0;
            ServiceStatus.dwCurrentState  = SERVICE_STOPPED;
			SafeExit();
        default:
            break;
    }
    // Report current status
    SetServiceStatus(hStatus, &ServiceStatus);
    return;
}
