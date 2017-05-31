/*
 * Copyright (c) 2017 Timothy Savannah - All Rights Reserved.
 *
 * Not licensed by any means yet, private code until in better state.
 *
 * TODO: Lots of cleanups, consolidations
 * TODO :Better fixups with shebang
*/
#include <features.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <limits.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* If PATH_MAX is not defined (max length of a path), use 4096.
 *   POSIX defines it as 256. Some other systems define as 1024.
 *   Linux defines as 4096
 *   So go with the max. If some system out there has a longer
 *   PATH_MAX, it damnwell better define it!
 */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* If legacy and deprecated name is available but not modern, fix it for them. */
#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
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

/* EXIT_CODE_FAILED_LAUNCH - Exit code returned when cannot launch
 * sub program
 */
#define EXIT_CODE_FAILED_LAUNCH ( 127 )

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

#define USEC_TO_SECONDS 1000000.0

#define convertSecondsToUsec(x) ( (x) * USEC_TO_SECONDS )

#define NSEC_TO_SECONDS 1000000000.0

#define convertSecondsToNsec(x) ( (x) * NSEC_TO_SECONDS )

#define FORCE_DONT_OPTIMIZE( x ) __asm__ __volatile__("" :: "m" ( ( x ) ))
#ifdef __GNUC__

#define likely __builtin_expect( !!(x), 1 )
#define unlikely __builtin_expect( !!(x), 0 )


#else

#define likely ( !!(x) )
#define unlikely ( !!(x) )

#endif

#ifndef __USE_GNU
#define environ __environ
#endif


typedef struct {
    pid_t childPid;
    int childIsStarted;
    int childExitCode;
} SharedData;


volatile static SharedData *SharedData__init(void)
{
    volatile SharedData *sharedData;
    errno = 0;
#if defined(MAP_ANONYMOUS)
    sharedData = (SharedData *)mmap(NULL, sizeof(SharedData) + 1, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
#else
    FILE *_mmapFile;
    char _zeroArray[ sizeof(SharedData) + 1 ] = { 0 };

    _mmapFile = tmpfile(); /* File is automatically deleted when the program closes*/

    /* Fill with enough zeros to mmap */
    write(fileno(_mmapFile), _zeroArray, sizeof(SharedData) + 1);
    rewind(_mmapFile);

    sharedData = (volatile SharedData *)mmap(NULL, sizeof(SharedData) + 1, PROT_READ | PROT_WRITE, MAP_SHARED, fileno(_mmapFile), 0);
#endif
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
    
static inline void SharedData__destroy(volatile SharedData *sharedData)
{
    munmap( (SharedData *)sharedData, sizeof(SharedData) + 1 );
}

static inline void setChildPid(volatile SharedData *sharedData, pid_t newPid)
{
    sharedData->childPid = newPid;
    msync( (SharedData *)sharedData, sizeof(SharedData) + 1, MS_SYNC);
}

static inline void markChildStarted(volatile SharedData *sharedData)
{
    sharedData->childIsStarted = 1;
    msync( (SharedData *)sharedData, sizeof(SharedData) + 1, MS_SYNC);
}

static char *findEnvironVar(char *name)
{
    char *ret;
    char *markStart;
    char *markEnd;
    int i;
    int strlenVar;

    strlenVar = strlen(name);

    for(i=0; environ[i]; i++)
    {
        markStart = environ[i];
        markEnd = strchr(markStart, '=');
        if( markEnd == NULL )
            continue; /* ??? Not suppoed to happen */
        if ( strlenVar == (markEnd - markStart) &&  strncmp(name, markStart, markEnd - markStart ) == 0 )
        {
            strlenVar = strlen(markEnd + 1);
            ret = malloc( sizeof(char) * strlenVar + 1);
            strcpy(ret, markEnd + 1);
            ret[strlenVar] = '\0';
            return ret;
        }
    }
    return NULL;
 }


static char* checkPath(char *path, char *cmd)
{
    int pathLen;
    char *pathCopy;

    int tryVarLen;
    char *tryVar;

    int cmdLen;

    cmdLen = strlen(cmd);

    pathCopy = NULL;
    tryVar = NULL;
    
    pathLen = strlen(path);
    if ( pathLen == 0 )
        return NULL;
    if (path[pathLen-1] == '/' && pathLen != 1)
    {
        int notSlashIdx;
        notSlashIdx = pathLen - 2;
        while ( notSlashIdx > 0 && path[notSlashIdx] == '/' )
            notSlashIdx -= 1;

        /* If we cleared out all the way, the path was for / (or //, etc),
         *   otherwise we have stripped the final slash */
        pathCopy = malloc( notSlashIdx + 2);
        if ( notSlashIdx > 0 )
        {
            strncpy(pathCopy, path, notSlashIdx);
            pathCopy[notSlashIdx ] = '\0';

            pathLen = notSlashIdx;
        }
        else
        {
            pathCopy[0] = '/';
            pathCopy[1] = '\0';

            pathLen = 1;
        }
    }

    tryVar = malloc( cmdLen + 1 + pathLen + 1);
    sprintf(tryVar, "%s/%s", pathCopy ? pathCopy : path, cmd);

    struct stat statBuf;
    
    if (  stat(tryVar, &statBuf) < 0 )
        goto check_path_cleanup_and_exit;

    /* TODO: Check for execute */
    return tryVar;

    if( pathCopy != NULL )
        free(pathCopy);

check_path_cleanup_and_exit:
    if( pathCopy != NULL )
        free(pathCopy);

    if ( tryVar != NULL )
        free(tryVar);

    return NULL;
}
        

static char* findPath(char *var)
{
    if ( strchr(var, '/') != NULL )
        return var;

    char *pathVar;
    char *prev;
    char *cur;
    char *ret;
    char tmpPath[PATH_MAX + 1];

    pathVar = findEnvironVar("PATH");
    if ( !pathVar || pathVar[0] == '\0' )
    {
        fputs("Warning: PATH is empty or not set. Cannot resolve paths.\n", stderr);
        return NULL;
    }

    prev = pathVar;
    strtok(pathVar, ":");
    cur = strtok(NULL, ":");


    do
    {
        strncpy(tmpPath, prev, cur - prev);
        tmpPath[cur - prev] = '\0';

        ret = checkPath(tmpPath, var);
        if ( ret && ret != prev )
            goto checkpath_exit;

        prev = cur;
        cur = strtok(NULL, ":");

    }while( cur != NULL );
    
checkpath_exit:
    free(pathVar);
    return ret;

}

static double strtodouble(char *str)
{
    double ret;
    char *endptr = { 0 };

    errno = 0;

    if ( *str )
    {
        ret = strtod(str, &endptr);
    }
    else
    {
        errno = 1; /* Simulate error */
        return 0;
    }

    if ( errno != 0 || ( !ret && endptr == str) || ( *endptr != '\0' ) )
    {
        return 0;
    }

    return ret;
}

static char *getShebang(char *filename, int *errorCode)
{
    /* TODO: Check access for read and execute */
    FILE *f;
    char buff[ PATH_MAX ];
    char *tmp, *ret;
    size_t bytesRead;

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
    strncpy(ret, buff + 2, tmp - buff - 2- 1);
    ret[tmp - buff - 2 - 1] = '\0';

    return ret;
}

static inline double clockdiff(struct timespec *start, struct timespec *end)
{
    double diffTime;

    diffTime = (end->tv_sec - start->tv_sec) + ( (end->tv_nsec - start->tv_nsec) / NSEC_TO_SECONDS );

/*    printf("diffTime is: %lf\n", diffTime); */

    return diffTime;
}

static inline struct timespec secondsToTimespec(double seconds)
{
    struct timespec sleepTime;
    sleepTime.tv_sec = (long int)seconds;

    seconds -= (long int)seconds;

    sleepTime.tv_nsec = convertSecondsToNsec(seconds);

    return sleepTime;
}

    

static inline void sleepTimespec(struct timespec *sleepTime)
{
    nanosleep(sleepTime, NULL);
}

static inline void sleepSeconds(double seconds)
{
    struct timespec sleepTime;

    sleepTime = secondsToTimespec(seconds);

    sleepTimespec(&sleepTime);

}

int main(int argc, char* argv[])
{
    int i;

    double timeoutSeconds = -1.0;
    double gracefulTimeout = GRACEFUL_NONE;

    char **cmdMarker = NULL;
    char *tmp;
    int exitCode = EXIT_CODE_UNKNOWN;

    volatile SharedData *sharedData;

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
                errno = 0;
                gracefulTimeout = strtodouble(tmp+1);
                if ( errno != 0 )
                {
                    fprintf(stderr, "Provided graceful timeout is not a valid floating-point number. Got: %s\n", argv[i]);
                    return EXIT_CODE_ARG_VALUE_ERROR;
                }
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

            timeoutSeconds = strtodouble(argv[i]);
            if ( errno )
            {
                fprintf(stderr, "Invalid argument, expected [timeout seconds] to be a float. Got: %s\n", argv[i]);
                return EXIT_CODE_ARG_VALUE_ERROR;
            }
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

    sharedData = SharedData__init();
    if ( sharedData == NULL )
    {
        return EXIT_CODE_UNKNOWN;
    }

    int myErr;
    /* TODO: Needs to search PATH */
    char *shebang;
    char *newCmd;
    char **newCmdMarker = NULL;

   
    newCmd = findPath(cmdMarker[0]);
    if ( newCmd == NULL || newCmd == cmdMarker[0] )
    {
        newCmd = cmdMarker[0];
    }

    shebang = getShebang(newCmd, &myErr);

    if ( myErr )
        return myErr;

    int j = 0;
    if ( shebang != NULL )
    {
        /* TODO: Needs to split shebang */
//        printf("Allocating sizeof(char *)[%d] * (argc[%d] - (cmdMarker - argv)[%d] + 2 = %d\n", sizeof(char *), argc, (cmdMarker - argv), sizeof(char *) * (argc - (cmdMarker - argv)  + 2 ));
        newCmdMarker = calloc( sizeof(char *), (argc - (cmdMarker - argv)  ) + 3 );
        newCmdMarker[0] = shebang;
        i = 1;
    }
    else
    {
//        printf("Allocating sizeof(char *)[%d] * (argc[%d] - (cmdMarker - argv)[%d] + 1 = %d\n", sizeof(char *), argc, (cmdMarker - argv), sizeof(char *) * ( (argc - (cmdMarker - argv))  + 1 ));
        newCmdMarker = calloc( sizeof(char *), (argc - (cmdMarker - argv)  + 1 ) );
        newCmdMarker[0] = newCmd;
        i = 1;
        j = 1;
    }
    
    
    for(; j == 0 || cmdMarker[j] != NULL; i++, j++)
    {
        int thisLen;
        thisLen = strlen(cmdMarker[j]);
        newCmdMarker[i] = malloc( sizeof(char) * (thisLen + 1));
        strcpy(newCmdMarker[i], cmdMarker[j]);
        newCmdMarker[i][thisLen] = '\0';
    }

/*    for ( i=0; ; i++)
    {
        if ( newCmdMarker[i] == NULL )
        {
            fprintf(stderr, "newCmdMarker[i(%d)] = NULL\n", i);
            break;
        }
        else
        {
            fprintf(stderr, "newCmdMarker[i(%d)] = %s\n", i, newCmdMarker[i]);
        }
    }
*/
    
    pid_t childPid;
    childPid = fork();
    FORCE_DONT_OPTIMIZE( sharedData );
    if ( childPid == 0 )
    {
        /* Child - this is going to be exec. */
        markChildStarted(sharedData);
        if ( execvp(newCmdMarker[0], newCmdMarker) != 0 )
        {
            fputs("Failed to launch program!\n", stderr); 
        }
        return EXIT_CODE_FAILED_LAUNCH; 
    }
    else
    {
        int exitStatus;
        double pollTime;

        struct timespec startTime;
        struct timespec curTime;

        struct timespec sleepTime;

        int waitPidRet;


        setChildPid( sharedData, childPid);


        do {
            sleepSeconds( .01 );
        } while ( ! sharedData->childIsStarted );


        clock_gettime( CLOCK_REALTIME, &startTime);

        pollTime = timeoutSeconds / 20.0;
        if ( pollTime > .1 )
            pollTime = .1;


        sleepTime = secondsToTimespec( pollTime );
        do
        {
            sleepTimespec(&sleepTime);
            clock_gettime( CLOCK_REALTIME, &curTime);
            errno = 0;
            waitPidRet = waitpid( childPid, &exitStatus, WNOHANG );
            if ( waitPidRet == 0 )
                continue;
            if ( WIFEXITED(exitStatus) )
            {
                exitCode = WEXITSTATUS(exitStatus);
/*                printf ( "Exited with code: %d\n", exitCode); */
                goto cleanup_and_exit;
            }
            else if ( waitPidRet < 0 )
            {
                exitCode = EXIT_CODE_UNKNOWN;
                goto cleanup_and_exit;
            }

        } while ( clockdiff(&startTime, &curTime) < timeoutSeconds );

/*        printf(" After loop.\n" ); */
        exitCode = EXIT_CODE_KILLED;

        if ( gracefulTimeout == GRACEFUL_NONE )
        {
            kill(childPid, SIGKILL);
            goto cleanup_and_exit;
        }

        pollTime = gracefulTimeout / 20.0;
        if ( pollTime > .1 )
            pollTime = .1;

        kill(childPid, SIGTERM);

        sleepTime = secondsToTimespec( pollTime );
        clock_gettime( CLOCK_REALTIME, &startTime);
        do
        {
            sleepTimespec(&sleepTime);
            clock_gettime( CLOCK_REALTIME, &curTime);
            waitPidRet = waitpid( childPid, &exitStatus, WNOHANG );
            if ( waitPidRet == 0 )
                continue;
            if ( WIFEXITED(exitStatus) )
            {
                goto cleanup_and_exit;
            }
            else if(waitPidRet < 0)
            {
                goto cleanup_and_exit;
            }

        } while ( clockdiff(&startTime, &curTime) < gracefulTimeout );

        kill(childPid, SIGKILL);

cleanup_and_exit:

        SharedData__destroy(sharedData);

        if ( newCmdMarker[0] != NULL )
        {
            for(i=1; newCmdMarker[i] != NULL; i++)
                free(newCmdMarker[i]);
        }

        if ( newCmdMarker[i] != cmdMarker[0] )
            free(newCmdMarker[0]);

        free(newCmdMarker);
    }

    return exitCode;
}
