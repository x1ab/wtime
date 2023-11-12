#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdio.h>
#include <process.h>
#include <errno.h>

int main(int argc, char** argv)
{
	if (argc < 2) 
	{
		printf("usage: wintiming progname [args]\n");
		return -1;
	}

	++argv;

	intptr_t result;
	DWORD t1, t2;

	t1 = GetTickCount();
	result = _spawnvp(_P_WAIT, argv[0], argv);
	t2 = GetTickCount();

	if (result == -1 && errno != 0)
	{
		switch (errno)
		{
			case E2BIG: 
				printf("argument list exceeds 1024 bytes\n");
				break;

			case EINVAL: 
				printf("mode argument is invalid\n");
				break;

			case ENOENT: 
				printf("file or path is not found\n");
				break;

			case ENOEXEC: 
				printf("specified file is not executable or has invalid format\n");
				break;

			case ENOMEM:
				printf("no memory to execute '%s'\n", argv[0]);
				break;

			default:
				printf("unknown error %d\n", errno);
		}
	}

	printf("elapsed time: %lu ms\n", t2-t1);

	return result;
}
