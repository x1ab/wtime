Simple timing wrapper for Windows executables (similar to UNIX `time`)

Usage:

	wtime executable [args...]

Notes:

  - `executable` must really be a valid executable, not just
    a built-in shell command. (Add a shell command explicitly
    for those, like: `wtime cmd /c echo OK`)

  - To pass arguments with spaces to the executable (e.g. a file
    name "one two.txt"), you'd need to use the mildly perverted
    triple-quote syntax with CMD:

        wtime busybox cat """one two.txt"""

    For anything more complicated, honestly, call 911.
    (Shockingly, for PowerShell (which I'm not familiar with),
    even with the help of Claude 3.5 (Sonnet), I couldn't find
    the proper quote-escaping syntax for the above example.)
