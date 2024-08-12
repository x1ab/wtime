Simple timing wrapper for Windows executables (similar to UNIX `time`)

Usage:

	wtime executable [args...]

Notes:

	- "executable" must really be a valid executable, not just
	  a built-in shell command. (Add a shell command explicitly
	  for those, like: `cmd /c echo OK`)
