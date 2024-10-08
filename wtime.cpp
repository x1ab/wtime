//#include "Args.hpp" <- Disabled for playing with #3...

#include <chrono>
#include <iostream>
#include <string>
#include <string_view>
#include <cctype> // tolower
#include <vector>
//#include <algorithm> // transform, any_of
#include <cassert>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>


using namespace std; // You know, you're not _actually_ obliged to unconditionally
                     // torture yourself all the time with C++! ;-p Also: it's MY code.


const char* TOOLNAME = "wtime";
const char* VERSION = "2.3.0";


//============================================================================
// Config...
//============================================================================
struct CFG
{

	string Report_Time_Unit  = "s"; // "s", "ms", "min", "mins", or the full words in plural
	bool   Verbose           = false;
	bool   Results_To_Stdout = false; // or stderr
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
namespace sys
//----------------------------------------------------------------------------
{
	//----------------------------------------------------------------------------
	// Detect if this is 32 or 64 bit build
	//----------------------------------------------------------------------------
	template <unsigned> unsigned    BitArch();
	template <>         unsigned    BitArch<4u>() { return 32; }
	template <>         unsigned    BitArch<8u>() { return 64; }
	inline const char* BitArchTag()  { return BitArch<sizeof(size_t)>() == 32 ? "32" : "64"; }

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
	static bool run(string_view cmdline, int* exitcode = nullptr, DWORD* w32_error = nullptr)
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
} // namespace sys

//----------------------------------------------------------------------------
class CmdLine
//----------------------------------------------------------------------------
// Usage:
//	string cmd = CmdLine(argv, argc).build();
//	
//!! Add this to Args!
//----------------------------------------------------------------------------
{
public:
	static string quote(const string& arg) { return escape_win32(arg); } //!! Quote AND escape then, actually...

	static string escape_win32(const string& arg)
	// Written by Claude 3.5 Sonnet; reviewed by ChatGPT 4o... Only tested with spaces!
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

	static string build(char const* const* argv, int argc)
	{
		vector<string> args(argv, argv + argc);
		string cmdline;
		for (const auto& arg : args) {
			if (!cmdline.empty()) cmdline += ' ';
			cmdline += escape_win32(arg);
		}
		return cmdline;
	}
}; // class cmdline


//============================================================================
// Main...
//============================================================================

Timer bailout_timer(Timer::Start);
auto& normal_out = cfg.Results_To_Stdout ? cout : cerr;

int main(int argc, char* argv[], [[maybe_unused]] char* envp[])
{
	// Set the console to UTF-8 (to be restored on exit)
	//!! Doesn't seem to help, though, in certain (common?) scenarios! :-/
	//!! Eg. I can `echo ŐŰ` just fine directly, but passing that to `cmd /c`
	//!! would still strip the accents, no matter what!... :-o
	sys::ConsoleCP set(CP_UTF8);

//	Args args(argc, argv);

	if (argc < 2) {
		cerr
			<< TOOLNAME << " version " << VERSION
			<< " (" << sys::BitArchTag() << "-bit)"
			<< " -> https://github.com/x1ab/wtime"
			<< '\n'
			<< "Usage: " << TOOLNAME << " exename [args...]\n"
			<< R"(
Notes:

  - `exename` must be a standalone executable. (Shell built-ins can be run
    with explicit shell commands like: `wtime cmd /c echo OK`.)

    Batch files can be run directly (well, obviously still invoking CMD for
    them). The .cmd or .bat extension can be omitted, just like the .exe
    (or .com) of real binaries.

  - Quotes around parameters with spaces will be preserved. (Unlike the default
    behavior on Windows; so no need for e.g. the mildly perverted triple-quote
    syntax with CMD, like `wtime busybox cat """one two.txt"""`).

    There's also support for quoting (and honoring) escaped special chars using
    Win32's \ syntax (not the ^ syntax of CMD, sorry!), but for anything more
    complex, or if you find yourself stuck fighting this, honestly, call 911.

  - Wildcards are not expanded.

)";

		cerr	<< "(BTW, just for the fun of it: printing this took "
			<< bailout_timer.elapsed() * 1000
			<< " milliseconds.)\n";

		return -1;
	}

	++argv; --argc; //!! shift

	// Save with the original case for reporting:
	const string child_exe_original_case = argv[0]; assert(!child_exe_original.empty());
	// ...and convert to lowercase for later processing (yeah, without std::transform...):
	string child_exe_original_tmp = child_exe_original_case;
	for (char& c : child_exe_original_tmp)
		c = (char)std::tolower((unsigned char)c); //! For that sad casts: -> https://en.cppreference.com/w/cpp/string/byte/tolower
	const string child_exe_original = child_exe_original_tmp;

	string child_exe_escaped = CmdLine::quote(child_exe_original); assert(!child_exe_escaped.empty());
		// Need to manually escape the exe name. (Arg lists from CmdLine::build are escaped implicitly!)
	++argv; --argc; //!! shift
	string child_args = CmdLine::build(argv, argc); //!! Add this feature to Args!

	int child_exitcode; DWORD win32_error;
	Timer timer(cfg.Report_Time_Unit);

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
	// Retry support to amend the shortomings of run() (i.e. CreateProcess) -> #2...
	// Only an omitted ".exe" would be covered transparently by run.
	//!! Not adding the retry feature directly to run itself, to keep the timing closer to a clean run.
	//!! Quick measurements with explicit paths showed no difference, but repeated PATH lookups might!
	const string retry_with[] = {".com", ".bat", ".cmd"}; // REVERSE order of preference!
	auto retry = child_exe_original.ends_with(".exe") ? // Don't retry if it's an .exe!
	                0 : sizeof(retry_with)/sizeof(decltype(retry_with[0]));
	// Also don't retry if already has any of those extensions, or .exe:
	for (const auto& ext : retry_with)
		if (child_exe_original.ends_with(ext))
			retry = 0;
do_try:
	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	string cmdline = child_exe_escaped + " " + child_args;
	if (cfg.Verbose) normal_out << "Executing: " << cmdline <<"...\n";

	timer.start();
	bool run_succeeded = sys::run(cmdline, &child_exitcode, &win32_error);
	timer.stop();

	if (run_succeeded) {
		normal_out
			<< "Elapsed time: " << timer.elapsed()
			<< ' ' << cfg.Report_Time_Unit
			<< '\n';

		return child_exitcode;
	}

	// Errors...

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
	// Rertry with default extensions if not found?
	if (win32_error == ERROR_FILE_NOT_FOUND && retry) {
		--retry;
		// Append the next missing ext. for a(nother) try:
		child_exe_escaped = CmdLine::quote(child_exe_original + retry_with[retry]);
		goto do_try;
	}
	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	// Report the error
	cerr << "- Failed to run \""<< child_exe_original_case <<"\": ";
	switch (win32_error) {
		case ERROR_FILE_NOT_FOUND: cerr << "file not found"; break;
		case ERROR_PATH_NOT_FOUND: cerr << "path not found"; break;
		case ERROR_ACCESS_DENIED: cerr << "access denied"; break;
		case ERROR_INVALID_EXE_SIGNATURE:
		case ERROR_EXE_MARKED_INVALID:
		case ERROR_BAD_EXE_FORMAT:
		case ERROR_EXE_MACHINE_TYPE_MISMATCH:
		case ERROR_BAD_FORMAT: cerr << "invalid file type/format"; break;
		case ERROR_NOT_ENOUGH_MEMORY:
		case ERROR_OUTOFMEMORY: cerr << "not enough memory"; break;
		default: cerr << "unknown error: " << win32_error << ")";
	}
	cerr << "!\n";

	return -2;
}
