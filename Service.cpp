#include "main.h"
#include "service.h"

static SERVICE_STATUS ServiceStatus;
static SERVICE_STATUS_HANDLE hStatus;
static LPWSTR serviceName;

int service_register(const LPWSTR serviceName_)
{
	serviceName = serviceName_;
    int ret;
    SERVICE_TABLE_ENTRYW ServiceTable[] = {
        { serviceName, (LPSERVICE_MAIN_FUNCTIONW)service_main},
        {NULL, NULL}
    };

    ret = StartServiceCtrlDispatcherW(ServiceTable);
	VLOG(1) << "DEBUG: StartServiceCtrlDispatcher returned with: " << ret;
    return ret;
}

void service_main()
{
    ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS; 
    ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    ServiceStatus.dwWin32ExitCode = 0;
    ServiceStatus.dwServiceSpecificExitCode = 0;
    ServiceStatus.dwCheckPoint = 1;
    ServiceStatus.dwWaitHint = 0;

    hStatus = RegisterServiceCtrlHandlerW(
		serviceName,
        (LPHANDLER_FUNCTION)service_controlhandler);

    if (hStatus == (SERVICE_STATUS_HANDLE)0)
    {
        //Registering Control Handler failed"
		VLOG(1) << "DEBUG: Registering Control Handler failed with: " << hStatus;
        return;
    }

    SetServiceStatus(hStatus, &ServiceStatus);

    // Calling main
	VLOG(1) << "DEBUG: starting server from service.";
    ServiceStatus.dwWin32ExitCode = wmain();
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
