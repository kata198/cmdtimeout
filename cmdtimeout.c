/*
 * Copyright (c) 2017 Timothy Savannah - All Rights Reserved.
 *
 * Not licensed by any means yet, private code until in better state.
 *
 * TODO: -O2 seems to break
 * TODO: Lots of cleanups, consolidations
 * TODO :Better fixups with shebang
 * TODO :search PATH
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <limits.h>
#include <string.h>
#include <time.h>


#ifndef PATH_MAX
#define MAX_MATH 4096
#endif

void printUsage(void)
{
    fputs("Usage: cmdtimeout (Options) [timeout] -- [program name] (Program args)\n  Runs the command with a provided maximum alloted time.\n\nTODO: Options.\n", stderr);
}

/* GRACEFUL_NONE - No grace period */
#define GRACEFUL_NONE (-1 * (1 << 0))
/* GRACEFUL_CALC - Calculate graceful timeout */
#define GRACEFUL_CALC (-1 * (1 << 1))

/* EXIT_CODE_ARG_ERROR - Exit code when a required argment is missing */
#define EXIT_CODE_ARG_ERROR ( 250 )

/* EXIT_CODE_ARG_VALUE_ERROR - Exit code when an argument has wrong format
  or is otherwise invalid 
*/
#define EXIT_CODE_ARG_VALUE_ERROR ( 251 )

/* EXIT_CODE_KILLED - Exit code when timeout is exceeded and cmd 
  must be killed 
*/
#define EXIT_CODE_KILLED ( 180 )

/* EXIT_CODE_UNKNOWN - When something unknown happens and we don't
   gather a return code
*/
#define EXIT_CODE_UNKNOWN ( 254 )

/* CHILD_NO_EXIT_CODE - An impossible exit code to mark that we have not got one from child  */
#define CHILD_NO_EXIT_CODE ( 300 )

#define USEC_TO_SECONDS 1000000

#define convertSecondsToUsec(x) ( (x) * USEC_TO_SECONDS )

typedef struct {
    pid_t childPid;
    int childIsStarted;
    int childExitCode;
} SharedData;


static SharedData *SharedData__init(void)
{
    SharedData *sharedData;
    errno = 0;
    sharedData = (SharedData *)mmap(NULL, sizeof(SharedData) + 1, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if ( sharedData == (void *)-1 )
    {
        fprintf(stderr, "Failed to allocated memory! Error: <%d> %s\n", errno, strerror(errno));
        return NULL;
    }

    sharedData->childPid = -1;
    sharedData->childIsStarted = 0;
    sharedData->childExitCode = CHILD_NO_EXIT_CODE;

    return sharedData;
}
    
static inline void SharedData__destroy(SharedData *sharedData)
{
    munmap(sharedData, sizeof(SharedData) + 1 );
}

static inline void setChildPid(SharedData *sharedData, pid_t newPid)
{
    sharedData->childPid = newPid;
    msync(sharedData, sizeof(SharedData) + 1, MS_SYNC);
}

static inline void markChildStarted(SharedData *sharedData)
{
    sharedData->childIsStarted = 1;
    msync(sharedData, sizeof(SharedData) + 1, MS_SYNC);
}

static char *getShebang(char *filename, int *errorCode)
{
    /* TODO: Check access for read and execute */
    FILE *f;
    char buff[ PATH_MAX ];
    char *tmp, *ret;
    size_t bytesRead;

    char *shebang = NULL;

    *errorCode = 0;
    errno = 0;
    f = fopen(filename, "r");
    if( !f )
    {
        fprintf(stderr, "Failed to open '%s'\n", filename);
        *errorCode = EXIT_CODE_ARG_VALUE_ERROR;
        return NULL;
    }
    bytesRead = fread(buff, 1, PATH_MAX + 1, f);
    if( bytesRead == 0 )
    {
        fprintf(stderr, "Failed to read '%s'\n", filename);
        fclose(f);
        *errorCode = EXIT_CODE_ARG_VALUE_ERROR; /* TODO: New error code for invalid program to execute */
        return NULL;
    }


    fclose(f);

    if ( bytesRead < 4 )
       return NULL;

    buff[bytesRead] = '\0';

    if( buff[0] != '#' || buff[1] != '!' )
        return NULL;

    tmp = strchr(buff + 2, '\n');
    if ( ! tmp )
       return NULL; /* No  newline? Bad shebang line. */

    tmp -= 1;

    ret = malloc( ((int)(tmp - buff - 2)) + 1);
    strncpy(ret, buff + 2, tmp - buff - 1);
    ret[tmp - buff - 1] = '\0';

    return ret;
}

int main(int argc, char* argv[])
{
    pid_t watcherPid, basePid;
    int i;

    double timeoutSeconds = -1.0;
    int gracefulTimeout = GRACEFUL_NONE;

    char **cmdMarker = NULL;
    char *tmp;
    int exitCode = EXIT_CODE_UNKNOWN;

    SharedData *sharedData;

    for( i=1; i < argc; i++)
    {
        if( strcmp( "--help", argv[i]) == 0 || strcmp("-h", argv[i]) == 0)
        {
            printUsage();
            return 0;
        }
        else if( strstr(argv[i], "-g") == argv[i] || strstr(argv[i], "--graceful") == argv[i])
        {
            tmp = strchr(argv[i], '=');
            if(!tmp)
            {
                gracefulTimeout = GRACEFUL_CALC;
            }
            else
            {
                /* TODO: Use my strtoint function*/
                errno = 0;
                gracefulTimeout = atoi(tmp+1);
            }
        }
        else if ( strcmp("--", argv[i]) == 0 )
        {
            if( i + 1 >= argc )
            {
                fputs("Missing command after '--'.\n\n", stderr);
                printUsage();
                return EXIT_CODE_ARG_ERROR;
            }

            cmdMarker = &argv[i + 1];
            break;
        }
        else
        {
            if ( timeoutSeconds != -1.0 )
            {
                fprintf(stderr, "Too many arguments at %d: '%s'\n\n", i, argv[i]);
                printUsage();
                return EXIT_CODE_ARG_ERROR;
            }

            timeoutSeconds = atof(argv[i]);
       }
    }

    if ( !cmdMarker )
    {
        fputs("Missing command separator, '--'. Command should follow '--'.\n\n", stderr);
        printUsage();
        return EXIT_CODE_ARG_ERROR;
    }

    if ( timeoutSeconds == -1.0 )
    {
        fputs("Missing timeout seconds.\n\n", stderr);
        printUsage();
        return EXIT_CODE_ARG_ERROR;
    }

    if ( gracefulTimeout == GRACEFUL_CALC )
    {
        gracefulTimeout = timeoutSeconds / 10.0;
        if ( gracefulTimeout > 5.0 )
            gracefulTimeout = 5.0;
        else if ( gracefulTimeout < .2 )
            gracefulTimeout = .2;
    }

    basePid = getpid();

    sharedData = SharedData__init();
    if ( sharedData == NULL )
    {
        return EXIT_CODE_UNKNOWN;
    }

    int myErr;
    /* TODO: Needs to search PATH */
    char *shebang = getShebang(cmdMarker[0], &myErr);
    char **newCmdMarker;

    if ( myErr )
        return myErr;

    if ( shebang != NULL )
    {
        printf("Shebang is \"%s\"\n", shebang);
        newCmdMarker = malloc( sizeof(char *) * (argc - (cmdMarker - argv) ) + 2 );
        newCmdMarker[0] = shebang;
        i = 1;
    }
    else
    {
        newCmdMarker = malloc( sizeof(char *) * (argc - (cmdMarker - argv) ) + 1 );
        i = 0;
    }
        
    for(; cmdMarker[i-1] != NULL; i++)
    {
        newCmdMarker[i] = cmdMarker[i-1];
    }
    newCmdMarker[i] = NULL;

    
    pid_t childPid;
    childPid = fork();
    if ( childPid == 0 )
    {
        /* Child - this is going to be exec. */
        markChildStarted(sharedData);
        if ( execvp(newCmdMarker[0], newCmdMarker) != 0 )
        {
            fputs("Failed to launch program!\n", stderr);
        }
        return EXIT_CODE_UNKNOWN; /* Should never be executed unless execvp fails. */
    }
    else
    {
        int exitStatus;
        double pollTime;

        time_t startTime;
        time_t timeWaited;

        useconds_t sleepUsec;


        setChildPid( sharedData, childPid);

        do {
            usleep ( convertSecondsToUsec( .01 ) );
        } while ( ! sharedData->childIsStarted );

        startTime = time(NULL);

        pollTime = timeoutSeconds / 20.0;
        if ( pollTime > .1 )
            pollTime = .1;

        sleepUsec = convertSecondsToUsec( pollTime );
        do
        {
            usleep ( sleepUsec );
            waitpid( childPid, &exitStatus, WNOHANG );
            if ( WIFEXITED(exitStatus) )
            {
                exitCode = WEXITSTATUS(exitStatus);
                printf ( "Exited with code: %d\n", exitCode);
                goto cleanup_and_exit;
            }
            timeWaited = time(NULL) - startTime;

        } while ( timeWaited < timeoutSeconds );

        exitCode = EXIT_CODE_KILLED;

        if ( gracefulTimeout == GRACEFUL_NONE )
        {
            kill(childPid, SIGKILL);
            goto cleanup_and_exit;
        }

        pollTime = gracefulTimeout / 20.0;
        if ( pollTime > .1 )
            pollTime = .1;
        sleepUsec = convertSecondsToUsec( pollTime );

        kill(childPid, SIGTERM);

        startTime = time(NULL);
        do
        {
            usleep ( sleepUsec);
            waitpid( childPid, &exitStatus, WNOHANG );
            if ( WIFEXITED(exitStatus) )
            {
                exitCode = WEXITSTATUS(exitStatus);
                goto cleanup_and_exit;
            }
            timeWaited = time(NULL) - startTime;

        } while ( timeWaited < gracefulTimeout );

        kill(childPid, SIGKILL);

cleanup_and_exit:

        SharedData__destroy(sharedData);
    }

    return exitCode;
}
