#ifndef _ARGS_HPP_

#include <string>
#include <vector>
#include <map>
#include <cstddef> // size_t
#include <cassert>
//#include <iostream> // cerr, for debugging

class Args // Tiny cmdline processor 1.2.1 (-> github.com/xparq/Args)
{
public:
	enum {	Defaults = 0, // Invalid combinations are not checked!
		RepeatAppends = 1,                     // repeated opt. takes further values (if it can) (default: override) (GH #16)
		RepeatIsError = 2,                     // repeated options set an error flag (default: override)
		//!! Not yet: UnknownIsError = 4,      // unknown options set an error flag (default: accept)
		//!! Not yet: UnknownIsPositional = 8, // unknown options are treated as positional args (default: accept) (GH #13)
		DashDashIsPositional         = 16,     // -- is just a positional arg (default: it makes any further args positional)
		//!! Not yet: DeleteUnknown  = 32,     // unknown options are ignored (default: accept)
		//!! Not yet: DeleteInvalid  = 64,     // remove known options that got incorrect # of params (GH #44)
		//!! Not yet: IgnoreCase     = 128,    // option names are case-insensitive (default: case-sensitive)
		//!! Not yet: Greedy         = 256,    // unknown options take as many params as they can (default: only --unknown= takes 1) (GH #43)
	};
	unsigned flags = Defaults;   // (not the enum to avoid some annoying restrictions)

	enum Error { None, ParameterMissing = 1, Duplicate = 2, Unimplemented = 0xffff };
	unsigned error = None;       // (not the enum to avoid some annoying restrictions)

	mutable int argc = 0; // 'mutable' to emphasize that these can be replaced for (and by) reparsing
	mutable char const* const* argv = nullptr; // "C++ MADE ME DO IT" ;) Being less uptight caused problems in corner cases

	typedef std::map<std::string, int> Rules;
	Rules known_options;  // { {"name", n_of_params_to_take}, ... }
	                      // Negative n means greedy: "at least n", until the next opt (or EOS)
	                      // NOTE: nonexistent entries will return 0, in accordance with NonGreedy (std::map zero-inits primitive types)

	Args(int argc, char const* const* argv, const Rules& rules = {}) // ...use this also when you need to set rules, but not flags
		: argc(argc), argv(argv), known_options(rules) { proc_next("", 0); }

	Args(int argc, char const* const* argv, unsigned flags, const Rules& rules = {}) // ...when you need to set flags, but not rules
		: flags(flags), argc(argc), argv(argv), known_options(rules) { proc_next("", 0); }

	Args() = default;
	Args(const Args&) = default;
	Args& operator=(const Args&) = default;

	bool parse(int argc_, const char* const* argv_, unsigned flags_ = Defaults, const Rules& rules_ = {})
		{ clear(); argc = argc_; argv = argv_; flags = flags_, known_options = rules_;
		  proc_next("", 0); return error == None; }

	bool reparse(unsigned flags_ = Defaults, const Rules& rules_ = {}) // Uses the original inputs
		{ return parse(argc, argv, flags_, rules_); }

	void clear() { named_args.clear(); positional_args.clear(); }

	// Check if 'opt' was set:
	bool operator[](const std::string& opt) const { return named().find(opt) != named().end(); }

	// Return nth param. of an option (first (0th) by default), or "":
	std::string operator()(const std::string& opt, size_t n = 0) const { return named().find(opt) == named().end()
	                                                            ? "" : (named().at(opt).size() <= n
	                                                                   ? "" : named().at(opt)[n]); }

	const std::vector<std::string>& positional() const { return positional_args; }
	      std::vector<std::string>& positional()       { return positional_args; }
	const std::map<std::string, std::vector<std::string>>& named() const { return named_args; }
	      std::map<std::string, std::vector<std::string>>& named()       { return named_args; }
		// Pedantry: if you want to prevent casual named()[...] calls to add missing keys
		// (std::map does that), use a const copy of the Args obj with op() and op[].

	// Remember: this is coming from the command that eventually launched the exe, so it
	// could be "anything"... E.g. no guarantee that it ends with ".exe" on Windows etc.
	std::string exename(bool keep_as_is = false, std::string ext = ".exe") const {
		// Anyway, if it has a path, remove it:
		std::string basename(std::string(argv[0]).substr(std::string(argv[0]).find_last_of("/\\") + 1));
		if (keep_as_is) { return basename; }
		else { return basename.substr(0, basename.rfind(ext)); }
	}

	bool operator !() const { return argc < 2; }
//	operator bool() const { return !!*this; }
	//! Enabling op bool would break args["opt"] due to a weird ambiguity, where the
	//! compiler would suddenly think it may also match the builtin "opt"[int]! :-o
	//! It's due to it trying a match with autoconverted bool->int before the "real thing".
	//! The hacky workaround below may be good enough tho (but certainly not "good":
	//! -> https://www.artima.com/articles/the-safe-bool-idiom)
	//! Just comment it out, if you feel offended! ;)
	operator const void*() const { return !(*this) ? nullptr : (void*)this; }

protected:
	std::map<std::string, std::vector<std::string>> named_args;
	std::vector<std::string> positional_args;

	void proc_next(const std::string& last_opt, int values_to_take, int next_arg_index = 1, bool options_done = false) {
		auto get_next_word = [&](){ return std::string(next_arg_index < argc ? argv[next_arg_index++] : ""); };
		auto handle_duplicate = [this](const std::string& opt){
			if (!(*this)[opt]) return; // Not a dup.
			if (  flags & RepeatIsError ) error |= Error::Duplicate;
			if (!(flags & RepeatAppends)) named_args[opt].clear(); // Reset its param. list...
		};
		auto arg = get_next_word();
		if (arg == "") return;
		if (options_done) goto process_as_positional; // Idon' wanna see no more if-nesting level...
		if (values_to_take > 0) { // last_opt still eating parameters?
	//std::cerr << "- eating '"<< arg <<"' as param. val. for " << last_opt << "\n";
			assert(!last_opt.empty());
			named_args[last_opt].emplace_back(arg); // Add current arg as new param. value to last_opt, and...
			return proc_next(last_opt, --values_to_take, next_arg_index, options_done); // ...continue with last_opt!
		}
		// Option (-x, /x, -aggregate, /aggregate, --thing)?...
		if ((arg[0] == '-' || arg[0] == '/') && arg.size() > 1) { // Option, or -- or // (note: -/ and /- are options!)
			std::string new_opt;
			if (arg[1] == '-' && arg.size() > 2) { // --long opt.
				new_opt = arg.substr(2); // OK, we don't check now... ;)
				// Extract any =value right now, because the next loop cycle can't (even see it)!
				//! This also allows --unknown=value, so no need for a rule for each arg
				auto eqpos = new_opt.find_first_of(":=");
#define ARG_HAS_EQ eqpos != std::string::npos
				if (ARG_HAS_EQ) new_opt = new_opt.substr(0, eqpos); // Chop off the =... from the opt. name
				handle_duplicate(new_opt);
				if (ARG_HAS_EQ) {
					if (arg.size() > 2/*for --*/ + eqpos + 1) { // Any value after the =?
		//std::cerr << "val: " << arg.substr(2, eqpos) << "\n";
						named_args[new_opt].emplace_back(arg.substr(2 + eqpos+1)); //! Don't crash on `--opt=`
						auto pc = known_options[new_opt]; // pc: configured param. count, or 0
						// We have taken (the) one offered value now, regardless of the arity rules!
						// But, if there's indeed a rule, we're still good, 'coz: if 0, then
						// the value would just be ignored be the app... :)  And if !0, then
						// we just need to keep taking more, as needed:
		//std::cerr << tmp << " params expected for [" << new_opt << "]\n";
						return proc_next(new_opt, pc<-1? pc+1 : (pc? pc-1 : 0), //!! The pc<-1 case is bogus/incomplete!
							next_arg_index, options_done);
					}
				} else values_to_take = known_options[new_opt]; // Take the next arg(s) instead, if needed
#undef ARG_HAS_EQ
			} else if (arg[1] != arg[0]) { // a real short opt, or short opt. aggregate
//!!				return proc_next("-" + arg.substr(1), next_arg_index, ...state/mode);
				for (auto c : arg.substr(1)) {
					new_opt = std::string(1, c);
					handle_duplicate(new_opt);
					named_args[new_opt];
				}
			} else { // -- (by default) or //whatever (always) are positional
				assert(arg[1] == arg[0]); // (In case I'd reshuffle the cond. above...)
				if (arg == "--" && !(flags & DashDashIsPositional)) {
					options_done = true;
					return proc_next(new_opt, 0, next_arg_index, options_done);
				}
				goto process_as_positional;
			}

			// We have a new option, process it...
			if (values_to_take < -1) {
				error = Unimplemented;
	//!!std::cerr << "- Not yet supporting variable number of option args.\n";
				// But it would involve using last_opt, hence the last/new distinction...
			}
			// Add the option with an empty param list to start with:
	//std::cerr << "ready to take next arg as param, if expects any.\n";
			named_args[new_opt];
			return proc_next(new_opt, known_options[new_opt], next_arg_index, options_done);
		}

	process_as_positional:
	//std::cerr << "- adding unnamed arg (or eating it as param): "<< arg <<"\n";
		if (values_to_take < 0) { // Well, there's still this greedy option-param case first, so...:
			named_args[last_opt].emplace_back(arg);
			return proc_next(last_opt, values_to_take, next_arg_index, options_done);
		} else {
			positional_args.push_back(arg); // Finally! :)
			return proc_next("", 0, next_arg_index, options_done);
		}
	};
};

#define _ARGS_HPP_
#endif
