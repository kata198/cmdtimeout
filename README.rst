cmdtimeout
==========

Execute an arbitrary command with a maximum given timeout


Use this to execute a command, but give it a max execution time.

If the timeout is reached, cmdtimeout can either terminate gracefully ( send terminate signal, wait a bit, then send kill ), or go straight to kill.


Example Usage
-------------

The following would execute a command "doDbLoad" with some arguments.

"doDbLoad" will have 60 seconds to complete, otherwise it gets the terminate signal, up to 5 seconds to cleanup and exit, otherwise it is forcibly killed.

	cmdtimeout --graceful=5 60 -- ./doDbLoad --from=/data/db1.db --to=MyDatabase


The following executes a "find" with a 10 second timeout.

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


