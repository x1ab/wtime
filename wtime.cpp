#include "Args.hpp"

#include <chrono>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <cassert>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>


using namespace std; // You know, you're not _actually_ obliged to unconditionally
                     // torture yourself all the time with C++! ;-p Also: it's MY code.


string VERSION = "2.1.0";

// Config...
//----------------------------------------------------------------------------
struct CFG
{
	bool Time_In_Seconds = true; // or ms
	bool Verbose = false;
	bool Results_To_Stdout = false; // or stderr
} cfg;

// Lib...
//----------------------------------------------------------------------------
class ConsoleCP
{
	UINT originalCP;
	UINT originalOutputCP;

public:
	ConsoleCP(UINT newCP) {
		originalCP = GetConsoleCP(); // input CP
		originalOutputCP = GetConsoleOutputCP();
		SetConsoleCP(newCP);
		SetConsoleOutputCP(newCP);
	}

	~ConsoleCP() {
		SetConsoleCP(originalCP);
		SetConsoleOutputCP(originalOutputCP);
	}
};

bool run(string_view cmdline, int* exitcode = nullptr, DWORD* w32_error = nullptr)
// Returns true if a new process for cmdline was successfully created,
// regardless of whether the command itself succeeded or not.
// Use the optional out args. to check the actual results.
{
	if (cmdline.empty()) return false;

	STARTUPINFOA si = {sizeof(si)};
	PROCESS_INFORMATION pi;

	string cmdline_writable(cmdline);
	if (!CreateProcessA(NULL, &cmdline_writable[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
		auto lasterr = GetLastError();
		if (w32_error) {
			*w32_error = lasterr;
		} else {
			cerr << "- CreateProcess failed (LastError: " << lasterr << ")!" << endl;
		}
		return false;
	}

	WaitForSingleObject(pi.hProcess, INFINITE);

	if (exitcode) {
		DWORD w32_exitcode;
		GetExitCodeProcess(pi.hProcess, &w32_exitcode);
		*exitcode = (int)w32_exitcode;
	}

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	return true;
}

string escape(const string& arg)
// Written by Claude 3.5 Sonnet; NOT REVIEWED! Only tested with spaces.
{
	if (arg.find_first_of(" \t\n\v\"") == string::npos) {
		return arg;
	}

	string escaped = "\"";
	for (auto it = arg.begin(); ; ++it) {
		unsigned backslashes = 0;
		while (it != arg.end() && *it == '\\') {
			++it;
			++backslashes;
		}

		if (it == arg.end()) {
			escaped.append(backslashes * 2, '\\');
			break;
		} else if (*it == '"') {
			escaped.append(backslashes * 2 + 1, '\\');
			escaped.push_back(*it);
		} else {
			escaped.append(backslashes, '\\');
			escaped.push_back(*it);
		}
	}
	escaped.push_back('"');
	return escaped;
}

string build_cmdline(const vector<string>& args) {
	string cmdline;
	for (const auto& arg : args) {
		if (!cmdline.empty()) cmdline += ' ';
		cmdline += escape(arg);
	}
	return cmdline;
}


// Main...
//----------------------------------------------------------------------------
int main(int argc, char* argv[], [[maybe_unused]] char* envp[])
{
	// Set the console to UTF-8 (to be restored on exit)
	//!! Doesn't seem to help, though, in certain (common?) scenarios! :-/
	//!! Eg. I can `echo ŐŰ` just fine directly, but passing that to `cmd /c`
	//!! would still strip the accents, no matter what!... :-o
	ConsoleCP set(CP_UTF8);

	Args args(argc, argv);

	if (argc < 2) {
		cerr
			<< args.exename() << " version " << VERSION << '\n'
			<< '\n'
			<< "Usage: " << args.exename() << " exename [args...]\n"
			<< R"(
Notes:

  - `exename` must be a standalone executable. (Shell built-ins can be run
    via explicit shell commands, like: `wtime cmd /c echo OK`.)

  - Wildcards are not expanded.
  
  - Quotes around parameters with spaces will be preserved. (Unlike the default
    behavior on Windows; so no need for e.g. the mildly perverted triple-quote
    syntax with CMD, like `wtime busybox cat """one two.txt"""`).

    For anything more complicated (like passing params with escaped quotes
    etc.), honestly, call 911.

)";
		return -1;
	}

	++argv; --argc;
	auto exe = argv[0];

	auto& normal_out = cfg.Results_To_Stdout ? cout : cerr;

	//!! Add this feature to Args!...:
	string cmdline = build_cmdline(vector<string>(argv, argv + argc));
	if (cfg.Verbose) normal_out << "Executing: " << cmdline <<"...\n";

	int child_exitcode; DWORD win32_error;

	auto start = chrono::high_resolution_clock::now();
	bool result = run(cmdline, &child_exitcode, &win32_error);
	auto stop  = chrono::high_resolution_clock::now();

	if (result)
	{
		auto d = chrono::duration<float>(stop - start);
		float t = (cfg.Time_In_Seconds ? d.count() : chrono::duration_cast<chrono::milliseconds>(d).count());
		auto  unit = cfg.Time_In_Seconds ? "s" : "ms";
		
		normal_out << "Elapsed time: " << t << ' ' << unit << '\n';

		return child_exitcode;
	}

	// Errors...
	cerr << "- Failed to run \""<< exe <<"\": ";
	switch (win32_error)
	{
		case ERROR_PATH_NOT_FOUND:
		case ERROR_FILE_NOT_FOUND: cerr << "path not found"; break;
		case ERROR_ACCESS_DENIED: cerr << "access denied"; break;
		case ERROR_BAD_FORMAT: cerr << exe <<"invalid file format"; break;
		case ERROR_NOT_ENOUGH_MEMORY:
		case ERROR_OUTOFMEMORY: cerr << "not enough memory"; break;
		default: cerr << "unknown error: " << win32_error << ")";
	}
	cerr << "!\n";
}
