#include <stdio.h>
#include <signal.h>	
#include <unistd.h>	
#include <stdlib.h>
#include <string.h>	
#include <readline/readline.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_ARGS 30

void handleProcess(char** args);
int parseInput(char** parsedArr, char* input);

int main(void){
    char* prompt;
    while (prompt = readline("# ")) {
    	// 0. Register signal handlers

    	// 1. Print the prompt (#)
        

        if(prompt == NULL){
            continue;
        }

    	// 2. Grab and parse the input - NOTE: Make sure to remove the newline
    	// character from the input string (otherwise, you'll pass "ls\n" to
    	// execvp and there is no executable called "ln\n" just "ls")

        char *parsed[MAX_ARGS];

        //check for pipe
        char* pipeCheck1 = strtok(prompt, "|");
        char* pipeCheck2 = strtok(NULL, "|");
        if(pipeCheck2 != NULL){
            char *rightParsed[MAX_ARGS];
            int leftLength = parseInput(parsed, pipeCheck1);
            int rightLength = parseInput(rightParsed, pipeCheck2);

            int pipefd[2];
            pipe(pipefd);

            int cpid = fork();
            if(cpid==0){
                dup2(pipefd[1], 1);
                close(pipefd[0]);
                close(pipefd[1]);
                handleProcess(parsed);
                exit(0);
            }
            cpid = fork();
            if(cpid==0){
                dup2(pipefd[0], 0);
                close(pipefd[0]);
                close(pipefd[1]);
                handleProcess(rightParsed);
                exit(0);
            }

            close(pipefd[0]);
            close(pipefd[1]);
            wait(NULL);
            wait(NULL);
            continue;
        }

        int length = parseInput(prompt, parsed);

        //look for file redirection and handle as needed
        for(int x=0; x<length; x++ ){
            if(strcmp(parsed[x], ">")==0){
                int fd = open(parsed[x+1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
                dup2(fd, 1);
                close(fd);
                parsed[x] = NULL;
            } else if(strcmp(parsed[x], "<")==0){
                int fd = open(parsed[x+1], O_RDONLY, 0644);
                if(fd<0){
                    printf("File does not exist. Please try again.");
                    break;
                }
                dup2(fd, 0);
                close(fd);
                parsed[x] = NULL;
            } else if(strcmp(parsed[x], "2>")==0){
                int fd = open(parsed[x+1], O_RDWR, 0644);
                dup2(fd, 2);
                close(fd);
                parsed[x]=NULL;
            }
        }

    	// 3. Check for job control tokens (fg, bg, jobs, &) (for now just
    	// ignore those commands)

    	// 4. Determine the number of children processes to create (number of
    	// times to call fork) (call fork once per child) (right now this will
    	// just be one)
        int status;
        int cpid = fork();
        if(cpid==0){
            handleProcess(parsed);
            exit(0);
        }

        if (waitpid(cpid, &status, 0) == -1) {
            perror("waitpid");
        } else {
            if (WIFEXITED(status)) {
                printf("Child 1 exited with status %d\n", WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                printf("Child 1 terminated by signal %d\n", WTERMSIG(status));
            }
        }

    	// 5. Execute the commands using execvp or execlp - e.g. execOneChild()
    	// or execTwoChildren()

    	// 6. NOTE: There are other steps for job related stuff but good luck
    	// we won't spell it out for you
	}

	return 0;
}

void handleProcess(char** args){
    execvp(args[0], args);
}

int parseInput(char** parsedArr, char* input){
    int i = 0;
    char *token = strtok(input, " ");
    while (token != NULL && i < MAX_ARGS - 1) {
        parsedArr[i++] = token;
        printf("%s\n", token);
        token = strtok(NULL, " ");
    }
    parsedArr[i] = NULL;
    return i;
}

