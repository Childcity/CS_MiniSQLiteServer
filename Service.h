#ifndef _SERVICE_
#define _SERVICE_

#include <Windows.h>
#include "glog/logging.h"

int service_register(DWORD argc, LPTSTR argv[], const LPTSTR serviceName_);
void service_main(DWORD argc, LPTSTR argv[]);
void service_controlhandler(DWORD request);

template<typename T>
T *cpy_cstr_list(size_t argc, const T argv[])
{
	T *new_str_list = new T[argc];

	for (size_t i = 0; i < argc; i++)
	{
		T p1 = argv[i];
		size_t word_size = 0;

		//get word size
		while (*p1++)
			++word_size;

		//alloc memory for word
		new_str_list[i] = new char[word_size + 1];

		T p2 = new_str_list[i];

		//copy word
		p1 = argv[i];
		while (*p1)
			*p2++ = *p1++;

		*p2 = 0;
	}

	return new_str_list;
}
#endif