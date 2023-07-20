///////////////////////////////////////////////
// Muhammad Asad Tariq (21L-5266) --- BCS-4F //
// Operating Systems - Assignment #2 - Shell //
///////////////////////////////////////////////


// -- INCLUSION OF REQUIRED LIBRARIES: --

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>

#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>


// -- DEFINITIONS OF SYMBOLIC CONSTANTS: --

#define MAX_CMD_LEN 128 // maximum length of a command that can be given to the shell
#define MAX_HISTORY 10  // maximum number of recent commands stored in history
#define MAX_TOK_NUM 32  // maximum number of tokens in a command (tokens are delimited by spaces)
#define MAX_ARG_NUM 16  // maximum number of command-line arguments that can be provided to a program
#define MAX_ARG_LEN 32  // maximum length of a command-line argument that can be provided to a program

#define READ_END    0
#define WRIT_END    1

// -- DECLARATIONS OF FUNCTIONS: --

void displayWelcome();
void displayDocs();
void displayApology();
void displayFarewell();


// -- MAIN FUNCTION: --

int main(int argc, char* argv[])
{
    // -- INITIALISATIONS OF VARIABLES USED ACROSS SHELL: --

    int cmdNum = 0;                             // serial number of current command
    char cmdString[MAX_CMD_LEN] = "\n";         // to store command input given to shell
    
    int cmdLen = 0;                             // length of command input given to shell
    int cmdCount = 1;                           // no. of commands in input (in case of pipelined commands)

    int histCmd = 0;                            // temp num for historical command demanded by user
    char cmdHistory[MAX_HISTORY][MAX_CMD_LEN];  // to store recent MAX_HISTORY commands
    for (int i = 0; i < MAX_HISTORY; i++)
    {
        cmdHistory[i][0] = '\0';
    }

    char* cmdTokens[MAX_TOK_NUM];               // to store pointers to tokens of command
    for (int i = 0; i < MAX_TOK_NUM; i++)
    {
        cmdTokens[i] = NULL;
    }

    char* token = NULL;                         // temp pointer to a token
    int tokNum = 0;                             // counter for tokens
    int tokItr = 0;                             // another counter for tokens (used for creating argv[]'s)

    pid_t* procs = NULL;                        // pid's of child processes
    int** pipes = NULL;                         // fd's of unnamed pipes for IPC
    char*** args = NULL;                        // command-line argument arrays for child processes
    int* argn = NULL;                           // cmd-arg counts for the above

    bool concurrent = false;                    // command runs alongside shell or shell waits for it?

    // -- DISPLAY WELCOME MESSAGE: --

    displayWelcome();
    //displayDocs();

    // -- LOOP TO KEEP TAKING COMMANDS AS INPUT AND EXECUTING THEM: --

    bool runAgain = true;   // as long as runAgain is true, shell will keep running
    do
    {
        cmdNum++;

        // -- TAKE COMMAND INPUT FROM USER: --

        while (strcmp(cmdString, "\n") == 0)    // until non-empty input
        {
            printf("\nASH-#%d:\t\t", cmdNum);
            fgets(cmdString, MAX_CMD_LEN, stdin);
        }

        strcpy(cmdHistory[cmdNum % MAX_HISTORY], cmdString);    // saving command to history

        // -- CHECK FOR 'exit', '!!' OR '!N' SPECIAL COMMANDS: --

        histCmd = atoi(cmdString + 1);               // for use in '!N' check below

        if (strncmp(cmdString, "exit", 4) == 0)      // if command starts with 'exit', terminate shell
        {
            runAgain = false;
        }
        else if (strncmp(cmdString, "!!", 2) == 0 && cmdNum == 1)       // '!!' as first command
        {
            printf("ASH-#ERROR:\tThere is no previous command as this is the first command!\n");
            cmdNum--;
        }
        else if (strncmp(cmdString, "!", 1) == 0 && histCmd >= cmdNum)  // call for future command
        {
            printf("ASH-#ERROR:\tCommand %d has not been executed yet - current command is number %d!\n", histCmd, cmdNum);
            cmdNum--;   // because current command invalid, so will repeat and not include in history
        }
        else if (strncmp(cmdString, "!", 1) == 0 && histCmd != 0 && histCmd < cmdNum - 10)  // for very old command
        {
            printf("ASH-#ERROR:\tCommand %d is no longer in history, as history stores only the recent %d commands!\n", histCmd, MAX_HISTORY);
            cmdNum--;   // same as above (including in history could potentially result in infinite recursion)
        }
        else
        {
            if (strncmp(cmdString, "!!", 2) == 0)                       // repeat previous command
            {
                strcpy(cmdString, cmdHistory[(cmdNum - 1) % MAX_HISTORY]);
                strcpy(cmdHistory[cmdNum % MAX_HISTORY], cmdString);
                printf("\nASH-#%d:\t\t%s\n", cmdNum, cmdString);          // to echo prev command
            }
            else if (strncmp(cmdString, "!", 1) == 0 && histCmd != 0)   // repeat Nth command
            {
                strcpy(cmdString, cmdHistory[histCmd % MAX_HISTORY]);
                strcpy(cmdHistory[cmdNum % MAX_HISTORY], cmdString);
                printf("\nASH-#%d:\t\t%s\n", cmdNum, cmdString);          // to echo Nth command
            }
            
            // -- TOKENISE COMMAND: --

            cmdLen = strlen(cmdString);
            cmdString[cmdLen - 1] = ' ';    // replace '\n' by ' ' for easier tokenisation (as delim is space)

            do
            {
                token = strtok((token ? NULL : cmdString), " ");    // tokenise cmdString with ' ' as delimiter
                cmdTokens[tokNum++] = token;                        // store token ptr in cmdTokens array

                if (token && strcmp(token, "|") == 0)               // if '|' operator, then more commands
                {
                    cmdCount++;
                }
            }
            while (token);

            // -- PREPARATION FOR EXECUTION OF COMMANDS VIA CHILD PROCESSES: --

            procs = (pid_t*)malloc(cmdCount * sizeof(pid_t));                   // creating array of cmdCount pid's
            
            argn = (int*)malloc(cmdCount * sizeof(int));
            args = (char***)malloc(cmdCount * sizeof(char**));                  // creating cmdCount argv[] arrays
            for (int i = 0; i < cmdCount; i++)
            {
                args[i] = (char**)malloc(MAX_ARG_NUM * sizeof(char*));          // of max MAX_ARG_NUM arguments
                for (int j = 0; j < MAX_ARG_NUM; j++)
                {
                    args[i][j] = (char*)malloc(MAX_ARG_LEN * sizeof(char));     // having max length MAX_ARG_LEN
                }

                argn[i] = 0;
                while (cmdTokens[tokItr] != NULL && strcmp(cmdTokens[tokItr++], "|") != 0)
                {
                    strcpy(args[i][(argn[i])++], cmdTokens[tokItr - 1]);
                }
                args[i][argn[i]] = NULL;
            }

            if (cmdCount != 1)  // pipes only required if more than one command
            {
                pipes = (int**)malloc((cmdCount - 1) * sizeof(int*));           // (N-1) pipes for IPC b/w N procs
                for (int i = 0; i < cmdCount - 1; i++)
                {
                    pipes[i] = (int*)malloc(2 * sizeof(int));                   // 2 bcz READ & WRITE ends
                    if (pipe(pipes[i]) == -1)
                    {
                        perror("ASH-#ERROR:\tUnable to create pipe");
                    }
                }
            }

            // -- EXECUTION OF COMMANDS VIA CHILD PROCESSES: --

            for (int i = 0; i < cmdCount; i++)
            {
                if (i != 0)     // close write end of pipe when no more writing is to be done
                {
                    close(pipes[i - 1][WRIT_END]);
                }

                // -- CHECKING FOR CONCURRENCY: --

                if (strcmp(args[i][argn[i] - 1], "&") == 0)
                {
                    concurrent = true;
                    args[i][argn[i] - 1] = NULL;
                    (argn[i])--;
                    printf("proc %d is concurrent!\n", i);
                }

                // -- EXTRA: IMPLEMENTING 'cd' COMMAND: --

                if (strcmp(args[i][0], "cd") == 0)
                {
                    if (chdir(args[i][1]))
                    {
                        perror("ASH-#ERROR:\tUnable to change directory");
                    }
                }
                else
                {
                    // -- CREATING CHILD PROCESSES FOR EXECUTING COMMANDS: --

                    procs[i] = fork();

                    if (procs[i] > 0)
                    {
                        if (cmdCount != 1 || !concurrent)   // pipelined commands run in parallel by default
                        {
                            wait(NULL);
                        }
                    }
                    else if (procs[i] == 0)
                    {
                        // -- REDIRECTING IN / OUT FROM / TO PIPES: --

                        if (i != 0)                 // will read from pipe except for first
                        {
                            dup2(pipes[i - 1][READ_END], 0);
                        }

                        if (i != cmdCount - 1)      // will write to pipe except for last
                        {
                            dup2(pipes[i][WRIT_END], 1);
                        }

                        // -- IMPLEMENTING INPUT / OUTPUT REDIRECTION: --

                        //////////

                        int k = 0;
                        while (args[i][k])
                        {
                            if (strcmp(args[i][k], "<") == 0)
                            {
                                if (args[i][k + 1])
                                {
                                    int fd = open(args[i][k + 1], O_RDONLY);

                                    dup2(fd, 0);

                                    args[i][k] = " ";
                                    args[i][k + 1] = " ";
                                    args[i][k] = NULL;
                                }
                            }
                            else if (strcmp(args[i][k], ">") == 0 || strcmp(args[i][k], "1>") == 0)
                            {
                                if (args[i][k + 1])
                                {
                                    int fd = open(args[i][k + 1], O_WRONLY | O_CREAT, 0777);

                                    dup2(fd, 1);

                                    args[i][k] = " ";
                                    args[i][k + 1] = " ";
                                    args[i][k] = NULL;
                                }
                            }
                            else if (strcmp(args[i][k], "2>") == 0)
                            {
                                if (args[i][k + 1])
                                {
                                    int fd = open(args[i][k + 1], O_WRONLY | O_CREAT, 0777);

                                    dup2(fd, 2);

                                    args[i][k] = " ";
                                    args[i][k + 1] = " ";
                                    args[i][k] = NULL;
                                }
                            }

                            k++;
                        }

                        // -- RUNNING COMMANDS: --

                        if (strcmp(args[i][0], "ash-docs") == 0)
                        {
                            displayDocs();
                        }
                        else if (strcmp(args[i][0], "history") == 0)
                        {
                            printf("ASH-#HISTORY:\n");
                            if (cmdNum == 1)
                            {
                                printf("\tEMPTY!\n");
                            }

                            for (int i = cmdNum - 1; i >= cmdNum - 10 && i > 0; i--)
                            {
                                printf("\t#%d:\t%s", i, cmdHistory[i % MAX_HISTORY]);
                            }
                        }
                        else if (strcmp(args[i][0], "mkfifo") == 0)
                        {
                            if (mkfifo(args[i][1], 0777) == -1)
                            {
                                perror("ASH-#ERROR:\tUnable to change directory");
                            }
                        }
                        else
                        {
                            struct rlimit limit;        // to limit max time spent on process to prevent hanging
                            limit.rlim_cur = 1;
                            limit.rlim_max = 1;
                            setrlimit(RLIMIT_CPU, &limit);

                            execvp(args[i][0], args[i]);

                            exit(EXIT_FAILURE);                   // in case execvp was not successfully executed
                        }

                        exit(EXIT_SUCCESS);

                        //////////
                    }
                    else
                    {
                        perror("ASH-#ERROR:\tUnable to create new process");
                    }
                }

                

                if (i != 0)     // close read end of pipe when no more reading is to be done
                {
                    close(pipes[i - 1][READ_END]);
                }
            }

            // -- WRAP-UP AFTER EXECUTION OF COMMANDS VIA CHILD PROCESSES: --

            free(procs);

            for (int i = 0; i < cmdCount; i++)
            {
                for (int j = 0; j < MAX_ARG_NUM; j++)
                {
                    free(args[i][j]);
                }
                free(args[i]);
            }
            free(args);

            if (cmdCount != 1)
            {
                for (int i = 0; i < cmdCount - 1; i++)
                {
                    free(pipes[i]);
                }
                free(pipes);
            }
        }    
        
        // -- RESETTING VALUES OF VARIABLES FOR NEXT RUN OF SHELL: --

        strcpy(cmdString, "\n");

        cmdLen = 0;
        cmdCount = 1;

        for (int i = 0; i < MAX_TOK_NUM; i++)
        {
            cmdTokens[i] = NULL;
        }

        token = NULL;
        tokNum = 0;
        tokItr = 0;

        histCmd = 0;

        procs = NULL;
        pipes = NULL;
        args = NULL;
        argn = NULL;

        concurrent = false;
    }
    while (runAgain);
    
    // -- DISPLAY FAREWELL MESSAGE: --

    if (runAgain)   // if loop has been exited despite runAgain being true, some error must have occurred
    {
        displayApology();
        return -1;  // unsuccessful termination of shell
    }
    else            // otherwise, the loop terminated correctly because the user entered the 'exit' command
    {
        displayFarewell();
        return 0;   // successful termination of shell
    }
}


// -- DECLARATIONS OF FUNCTIONS: --

void displayWelcome()
{
    printf("\n");
    printf("\t ______________________________________________________________________________ \n");
    printf("\t|                                                                              |\n");
    printf("\t|---------------------| Welcome to ASH (Asad's SHell)! |-----------------------|\n");
    printf("\t|______________________________________________________________________________|\n");
    printf("\t|                                                                              |\n");
    printf("\t|------ ENTER 'ash-docs' TO VIEW THE SHELL'S DOCUMENTATION AT ANY POINT. ------|\n");
    printf("\t|______________________________________________________________________________|\n");
    printf("\n");
}

void displayDocs()
{
    printf("\n");
    printf("\t ______________________________________________________________________________ \n");
    printf("\t|                                                                              |\n");
    printf("\t|---------------------------| <ASH> DOCUMENTATION |----------------------------|\n");
    printf("\t|______________________________________________________________________________|\n");
    printf("\n");

    printf(" In addition to the usual shell commands, ASH supports the following special commands:\n");
    
    printf("\n\t+ 'exit':\n");
    printf("\t\tIf a command starts with the word 'exit', the shell will terminate,\n");
    printf("\t\tregardless of what follows.\n");
    
    printf("\n\t+ '!!':\n");
    printf("\t\tIf a command starts with '!!', the previous command will be repeated,\n");
    printf("\t\tregardless of the rest of the command. However, if it is the first command,\n");
    printf("\t\tno command will be carried out.\n");

    printf("\n\t+ '!N':\n");
    printf("\t\tIf a command starts with '!N', where N is a natural number,\n");
    printf("\t\tthe Nth command will be repeated, regardless of the rest of the command.\n");
    printf("\t\tHowever, if the Nth command is more than %d commands old by then,\n", MAX_HISTORY);
    printf("\t\tno command will be carried out.\n");

    printf("\n\t+ 'ash-docs'\n");
    printf("\n\t+ 'history'\n");
    printf("\n\t+ 'mkfifo'\n");
    printf("\n\t+ 'cd'\n");
    printf("\n\t+ '<, >, 1>, 2> and |': Note that there should be no command-line arguments after in/out-put redirectors.\n");
    printf("\t\tHowever, any number of '|' may be used.\n");
}

void displayApology()
{
    printf("ASH-#:\t\tSorry for the error you had to face!\n");
}

void displayFarewell()
{
    printf("ASH-#:\t\tThank you for using ASH!\n");
}
