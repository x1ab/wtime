Simple timing wrapper for Windows executables (similar to UNIX `time`)

Usage:

	wtime exename [args...]

Notes:

 - Run it with no parameters for more information!

 - A 22 year C version lying around (which couldn't quote/escape params.)
   is only 15 KB UPXed. :) The current (static-linked, UPXed) 32-bit C++
   version is also not huge at ~110K, but still a 8x increase. Sorry, me!
