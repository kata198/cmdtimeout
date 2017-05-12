# cmdtimeout
Execute any arbitrary command with a provided maximum run time.

If the alloted time is exceeded, the application will be terminated by one of two means:

**Immediate Kill (Default)**

The command is sent SIGKILL (or, if on windows, the equivilant) which forcibly stops execution immediately and closes the application.

**Graceful Kill (-g)**

In graceful mode, if the alloted time passes before the application completes, it is sent SIGTERM (or, if on windows, the equivilant), and then given a short time (configurable) to cleanup and exit.

If the grace time passes and the application still has not stopped, the kill switch is thrown.


Example Usage
-------------

The following would execute a command "doDbLoad" with some arguments.

"doDbLoad" will have 60 seconds to complete, otherwise it gets the terminate signal, followed by up to 5 seconds to cleanup and exit on its own, after which it is forcibly killed.

	cmdtimeout --graceful=5 60 -- ./doDbLoad --from=/data/db1.db --to=MyDatabase


The following executes a "find" with a 10 second maximum execution time.

	cmdtimeout 10 -- find . >> dirlist


Extended Help
-------------

	Usage cmdtimeout (Options) [num seconds] -- [cmd] ([cmd args])
	   Runs specfied command, with a given maximum timeout.


	The arguments and options to cmdtimeout should precede two dashes '--',
	  after which everything is executed.

	   Options:

		 -g(=N)  --graceful(=N)    Instead of sending SIGKILL after timeout,
									 send SIGTERM and wait a few seconds.
								   You can provide the seconds between signals as N.
								   Default is to wait the smaller of: 10% of timeout, or 5 seconds,

		 -s      --shell           Execute the command through a shell

		 -?      --help            Show this message


	Example:    cmdtimeout  -g 5 20 -- ./doWork --database=mydb.db
	  Gives "doWork" 20 seconds to complete, otherwise 
	  SIGTERM and allows 5 seconds before forcing KILL.

