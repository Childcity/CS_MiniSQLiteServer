#ifndef _SERVICE_
#define _SERVICE_
int service_register(const LPWSTR serviceName_);
void service_main();
void service_controlhandler(DWORD request);
#endif