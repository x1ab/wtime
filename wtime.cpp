#include "Args.hpp"

#include <cassert>
#include <iostream>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <process.h>
#include <errno.h>

int main(int argc, char* argv[], [[maybe_unused]] char* envp[])
{
	Args args(argc, argv);

	using namespace std;

	if (argc < 2) 
	{
		cerr << "Usage: " << args.exename() << " progname [args]\n";
		return -1;
	}

	++argv; --argc;

	cout << "Executing \""<< argv[0] <<"\"";
	if (argc > 1) {
		cout << " with: ";
		cout << "[...]";
		cout << "\n";
	} else {
		cout << "...\n";
	}

	DWORD t1, t2;
	t1 = GetTickCount();
		auto result = (int)_spawnvp(_P_WAIT, argv[0], argv);
	t2 = GetTickCount();

	if (result == -1)
	{
		switch (errno)
		{
			case E2BIG: 
				cerr << "argument list exceeds 1024 bytes\n";
				break;

			case EINVAL: 
				cerr << "mode argument is invalid\n";
				break;

			case ENOENT: 
				cerr << "file or path is not found\n";
				break;

			case ENOEXEC: 
				cerr << "specified file is not executable or has invalid format\n";
				break;

			case ENOMEM:
				cerr << "no memory to execute " << args.exename() << "\n";
				break;

			default:
				cerr << "unknown error: " << errno << "\n";
		}
	}

	cout << "elapsed time: " << t2-t1 << "ms\n";

	return result;
}
