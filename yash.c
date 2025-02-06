#include <stdio.h>
#include <signal.h>	
#include <unistd.h>	
#include <stdlib.h>
#include <string.h>	
#include <readline/readline.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>

#define MAX_ARGS 30

typedef struct Job {
    int jobId;
    char* name;
    int pid;
    int pgid;
    char* status; // 0 = running, 1 = stopped, 2 = done
    int isBackground; // 0 = false, 1 = true
    struct Job* next;
} Job;

void handleProcess(char** args);
int parseInput(char** parsedArr, char* input);
void addJob(int jobId, char* name, int pid, int pgid, char* status, int isBackground);
void removeJob(int pid);
int handleRedirection(char** parsed, int length);
Job* getPlusJob();
void sigintHandler(int signum);
void sigstpHandler(int signum);
void sigchldHandler(int signum);
void checkBackgroundJobs();

Job* head = NULL;
int JobId = 1;

int main(void){

    char* prompt;
    while ((prompt = readline("# "))) {
    	
        // Signal handlers ignore when not in process
        signal(SIGINT, SIG_IGN);
        signal(SIGTSTP, SIG_IGN);
        signal(SIGTTOU, SIG_IGN);
        signal(SIGCHLD, sigchldHandler);

        if(prompt == NULL){
            continue;
        }

        // Handles builtin job command
        if(strcmp(prompt, "jobs") == 0){
            Job* current = head;
            Job* plus = getPlusJob();
            while (current != NULL) {
                printf("[%d] %s %s  %s\n", current->jobId, ((current == plus) ? "+" : "-") ,current->status, current->name); 
                current = current->next;
            }
            free(prompt);
            continue;
        }

        // Handles builtin fg command
        if(strcmp(prompt, "fg") == 0){
            Job* plus = getPlusJob();
            if(plus != NULL){
                plus->status = "RUNNING";
                plus->isBackground = 0;
                tcsetpgrp(STDIN_FILENO, plus->pgid);
                kill(-plus->pgid, SIGCONT);
                int status;
                waitpid(plus->pid, &status, WUNTRACED);
                if (WIFSTOPPED(status)) {
                    //printf("\n[%d] + Stopped %s\n", plus->jobId, plus->name);
                    plus->status = "STOPPED";
                }
                tcsetpgrp(STDIN_FILENO, getpgrp());
                if (!WIFSTOPPED(status)) {
                    removeJob(plus->pid);
                }
            }
            continue;
        }

        // Handles builtin bg command
        if(strcmp(prompt, "bg") == 0){
            Job* plus = getPlusJob();
            if(plus != NULL){
                plus->status = "RUNNING";
                plus->isBackground = 1;
                kill(-plus->pgid, SIGCONT);
            }
            continue;
        }


        int isBackground = 0;
        char *parsed[MAX_ARGS];

        int std_in = dup(0);
        int std_out = dup(1);
        int std_err = dup(2);

        char* promptCopy = strdup(prompt);

        // Check for pipe
        char* pipeCheck1 = strtok(prompt, "|");
        char* pipeCheck2 = strtok(NULL, "|");
        if(pipeCheck2 != NULL){
            char *rightParsed[MAX_ARGS];
            int leftLength = parseInput(parsed, pipeCheck1);
            int rightLength = parseInput(rightParsed, pipeCheck2);
            if(strcmp(rightParsed[rightLength-1], "&")==0){
                isBackground = 1;
                rightParsed[rightLength-1] = NULL;
                rightLength--;
            }

            int pipefd[2];
            pipe(pipefd);   //make the pipe
            int status;
            int rcpid;
            int lcpid = fork();
            if(lcpid == 0){   //child process code for pipe
                setpgid(0,0);
                rcpid = fork();
                if(rcpid == 0){
                    setpgid(0, getppid());
                    dup2(pipefd[0], 0);
                    close(pipefd[0]);
                    close(pipefd[1]);
                    if(isBackground == 0){
                        waitpid(lcpid, &status, WUNTRACED);
                    }
                    waitpid(lcpid, &status, 0);
                    waitpid(rcpid, &status, 0); 
                    if(handleRedirection(rightParsed, rightLength) == 0){
                        exit(1);
                    }
                    signal(SIGINT, sigintHandler);
                    signal(SIGTSTP, sigintHandler);
                    handleProcess(rightParsed);
                    exit(0);
                } else {
                    dup2(pipefd[1], 1);
                    close(pipefd[0]);
                    close(pipefd[1]);
                    if(handleRedirection(parsed, leftLength) == 0){
                        exit(1);
                    }
                    handleProcess(parsed);
                    fflush(stdout);
                    exit(0);
                }
            } else {    //parent process code for pipe
                close(pipefd[0]);
                close(pipefd[1]);
                if(isBackground == 0){
                    signal(SIGINT, sigintHandler);
                    signal(SIGTSTP, sigstpHandler);
                    setpgid(lcpid, lcpid);             
                    waitpid(-lcpid, &status, WUNTRACED);
                    tcsetpgrp(STDIN_FILENO, getpgid(0));
                    if (WIFSTOPPED(status)) {
                        addJob(JobId++, promptCopy, lcpid, lcpid, "STOPPED", isBackground);
                    }
                    signal(SIGINT, SIG_IGN);
                    signal(SIGTSTP, SIG_IGN);
                    signal(SIGTTOU, SIG_IGN);
                    fflush(stdout);

                }else {
                    addJob(JobId++, promptCopy, lcpid, lcpid, "RUNNING", isBackground);
                }

            }
            close(pipefd[0]);
            close(pipefd[1]);
            waitpid(lcpid, &status, 0);
            waitpid(rcpid, &status, 0);
            fflush(stdout);

            free(prompt);
            free(promptCopy);
            dup2(std_in, 0);
            dup2(std_out, 1);
            dup2(std_err, 2);       
            continue;
        }

        int length = parseInput(parsed, pipeCheck1);    //break input into tokens

        if(strcmp(parsed[length-1], "&")==0){   // checks for background process
            isBackground = 1;
            parsed[length-1] = NULL;
            length--;
        }

        if(handleRedirection(parsed, length)==0){    //look for file redirection and handle as needed, 0 means invalid
            continue;
        }

        //code for a normal command (1 fork then execvp)
        int status;
        int cpid = fork();
        if(cpid < 0) {
            perror("fork");
            exit(EXIT_FAILURE);
        }
        if(cpid==0){
            setpgid(0,0);
            handleProcess(parsed);  //actual process
            exit(0);
        } else {
            setpgid(cpid, cpid); 
            if(isBackground == 0){  //foreground process
                signal(SIGINT, sigintHandler);
                signal(SIGTSTP, sigstpHandler);
                tcsetpgrp(STDIN_FILENO, cpid);    //give child process control of terminal
                waitpid(-cpid, &status, WUNTRACED); 
                tcsetpgrp(STDIN_FILENO, getpgid(0));
                if (WIFSTOPPED(status)) {   //if the foreground process was stopped
                    addJob(JobId++, promptCopy, cpid, cpid, "STOPPED", isBackground);
                }
                signal(SIGINT, SIG_IGN);
                signal(SIGTSTP, SIG_IGN);
                signal(SIGTTOU, SIG_IGN);
                fflush(stdout);
            } else{   //background process
                addJob(JobId++, promptCopy, cpid, cpid, "RUNNING", isBackground);
            }
        }
        free(prompt);
        free(promptCopy);
        dup2(std_in, 0);
        dup2(std_out, 1);
        dup2(std_err, 2);       
    }

	return 0;
}

void handleProcess(char** args){
    signal(SIGINT, sigintHandler);  //enables keyboard interrupts
    signal(SIGTSTP, sigstpHandler);
    execvp(args[0], args);
    perror("execvp");
    exit(EXIT_FAILURE);
}

int parseInput(char** parsedArr, char* input){   //break input into tokens
    int i = 0;
    char *token = strtok(input, " ");
    while (token != NULL && i < MAX_ARGS - 1) {
        parsedArr[i++] = token;
        token = strtok(NULL, " ");
    }
    parsedArr[i] = NULL;
    return i;
}



Job* getPlusJob(){  //returns the most recent stopped process or recent bg process
    Job* current = head;
    Job* plus = NULL;
    while (current != NULL) {
        if (strcmp(current->status, "STOPPED")==0) {
            plus = current;
        } else if(plus==NULL || strcmp(plus->status, "RUNNING")==0) {    //implies that there aren't any stopped jobs yet
            plus = current;
        }
        current = current->next;
    }
    return plus;
}

int handleRedirection(char** parsed, int length){
    for(int x=0; x<length; x++ ){
        if(strcmp(parsed[x], ">")==0){  //output redirection
            int fd = open(parsed[x+1], O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
            dup2(fd, 1);
            close(fd);
            parsed[x] = NULL;
        } else if(strcmp(parsed[x], "<")==0){   //input redirection
            int fd = open(parsed[x+1], O_RDONLY, 0644);
            if(fd<0){
                printf("File does not exist. Please try again.\n");
                return 0;   //if unsuccessful
            }
            dup2(fd, 0);
            close(fd);
            parsed[x] = NULL;
        } else if(strcmp(parsed[x], "2>")==0){  //error redirection
            int fd = open(parsed[x+1], O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
            dup2(fd, 2);
            close(fd);
            parsed[x]=NULL;
        }
    }
    return 1;   //if successful
}

//creates job and adds to job list
void addJob(int jobId, char* name, int pid, int pgid, char* status, int isBackground) {
    Job* newJob = (Job*)malloc(sizeof(Job));
    newJob->jobId = jobId;
    newJob->name = strdup(name);
    newJob->pid = pid;
    newJob->pgid = pgid;
    newJob->status = strdup(status);
    newJob->isBackground = isBackground;
    newJob->next = NULL;
    
    if (head == NULL) {
        head = newJob;
    } else {
        Job* current = head;
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = newJob;
    }
}

void removeJob(int pgid) {  //removes jobs that have finished in bg and prints
    Job* current = head;
    Job* previous = NULL;
    while (current != NULL) {
        if (current->pgid == pgid) {
            if (previous == NULL) {
                head = current->next;
            } else {
                previous->next = current->next;
            }
            current->status = "DONE";
            printf("\n[%d] %s %s\n# ", current->jobId, current->status, current->name);
            free(current->name);
            free(current);
            return;
        }
        previous = current;
        current = current->next;
    }
}

//handlers for signals
void sigintHandler(int signum) {
    int pgid = tcgetpgrp(STDIN_FILENO);
    if (pgid != 0) {
        kill(-pgid, SIGINT);
    }
}

void sigstpHandler(int signum) {
    int pgid = tcgetpgrp(STDIN_FILENO);
    if (pgid != 0) {
        kill(-pgid, SIGTSTP);
    }
}

void sigchldHandler(int signum) {
    checkBackgroundJobs();
}

void checkBackgroundJobs() {    //scans for background jobs that have finished
    int status;
    Job* current = head;
    Job* prev = NULL;

    while (current != NULL) {
        int result = waitpid(current->pid, &status, WNOHANG);
        if (result == 0 || result == -1) {
            prev = current;
            current = current->next;
        } else {
            current->status = "DONE";
            removeJob(current->pid);
        }
    }
}

