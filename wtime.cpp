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


string VERSION = "2.2.0";


//============================================================================
// Config...
//============================================================================
struct CFG
{
	string Report_Time_Unit = "s"; // "s", "ms", "min", "mins", or the full words in plural
	bool Verbose = false;
	bool Results_To_Stdout = false; // or stderr
} cfg;


//============================================================================
// Lib...
//============================================================================

class Timer
{
	using time_point = std::chrono::time_point<std::chrono::high_resolution_clock>;
	enum Control { Null, Start, Hold, Stop };
	//If switching to enum class:
	//using Control::Null, Control::Start, Control::Hold, Control::Stop;

	string unit_;
	time_point start_;
	time_point stop_;
	Control state_ = Null; // Reusing the control enums (relying on common sense)...

	static auto read_()   { return chrono::high_resolution_clock::now(); }

public:
	Timer(string_view unit = "s") : unit_(unit) {}
	Timer(Control ctrl = Null, string_view unit = "s") : unit_(unit) {
		if (ctrl == Start) start();
	}

	auto start()   { start_ = read_(); state_ = Start; return start_; }
	auto stop()    { stop_  = read_(); state_ = Stop;  return stop_; }
	// Allow elapsed() to continue counting after a stop():
	//auto restart() { state_ = Start; } // Not quite this simple!... :)
	auto reset()   { state_ = Null; }

	template <typename NumT = float>
	NumT elapsed() { return state_ == Null ? 0
		: elapsed<NumT>(start_, state_ == Stop ? stop_ : read_()); }

	template <typename NumT = float>
	NumT elapsed(time_point start_time, time_point stop_time)
	{
		NumT duration_s = std::chrono::duration<NumT>(stop_time - start_time).count();

		NumT result;
		if      (unit_ == "s" || unit_ == "seconds")
				result = duration_s;
		else if (unit_ == "ms" || unit_ == "milliseconds")
				result = duration_s * 1000;
		else if (unit_ == "min" || unit_ == "mins" || unit_ == "minutes")
				result = duration_s / 60;
		else {
			cerr << "- Warning: unsupported time unit: \""<< unit_ <<"\"! Using seconds instead...\n";
			result = duration_s;
		}
		return result;
	}
};

//----------------------------------------------------------------------------
class ConsoleCP // RAII wrapper around setting/restoring the console code-page
//----------------------------------------------------------------------------
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

//----------------------------------------------------------------------------
bool run(string_view cmdline, int* exitcode = nullptr, DWORD* w32_error = nullptr)
//
// Returns true if a new process for cmdline was successfully created,
// regardless of whether the command itself succeeded or not.
// Use the optional out args. to check the actual results.
//
//----------------------------------------------------------------------------
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

//----------------------------------------------------------------------------
class CmdLine
//----------------------------------------------------------------------------
// Usage:
//	string cmd = CmdLine(argv, argc).build();
//	
//!! Add this to Args!
//----------------------------------------------------------------------------
{
	vector<string> args_;
public:
	CmdLine(char const* const* argv, int argc)
		: args_(argv, argv + argc)
	{}

	static string escape(const string& arg)
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

	string build() {
		string cmdline;
		for (const auto& arg : args_) {
			if (!cmdline.empty()) cmdline += ' ';
			cmdline += escape(arg);
		}
		return cmdline;
	}
}; // class cmdline


Timer reference_overhead_timer(Timer::Start);

//============================================================================
// Main...
//============================================================================
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

    For anything more complicated (like passing parameters with escaped quotes
    etc.), honestly, call 911.

)";

		cerr	<< "(BTW, just for the fun of it: printing this took "
			<< reference_overhead_timer.elapsed() * 1000
			<< " milliseconds.)\n";

		return -1;
	}

	++argv; --argc;
	auto exe = argv[0];

	auto& normal_out = cfg.Results_To_Stdout ? cout : cerr;

	string cmdline = CmdLine(argv, argc).build(); //!! Add this feature to Args!
	if (cfg.Verbose) normal_out << "Executing: " << cmdline <<"...\n";

	int child_exitcode; DWORD win32_error;

	Timer timer(cfg.Report_Time_Unit);
	timer.start();
	bool result = run(cmdline, &child_exitcode, &win32_error);
	timer.stop();

	if (result)
	{
		normal_out
			<< "Elapsed time: " << timer.elapsed()
			<< ' ' << cfg.Report_Time_Unit
			<< '\n';

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
