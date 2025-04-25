#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>

#define CMDLINE_MAX 512
#define ARGUMENT_MAX 16
#define TOKEN_MAX 32
#define MAX_COMMANDS 4   

typedef struct Command {
    char *argv[ARGUMENT_MAX + 1]; 
    int argc;                      
    char *output_file;             
    char *input_file;              
} Command;

typedef struct Job {
    Command commands[MAX_COMMANDS]; 
    int command_count;             
    int background;                
    pid_t pids[MAX_COMMANDS];      
    int completed;                 
    char cmdline[CMDLINE_MAX];     
} Job;

char cwd[CMDLINE_MAX];             
Job bg_job;                        
int have_bg_job = 0;               

void parse_command_line(char *cmdline, Job *job);
int execute_builtin(Command *cmd, const char *cmdline);
int execute_job(Job *job);
void check_background_job(void);
void free_job(Job *job);

int main(void)
{
    char cmdline[CMDLINE_MAX];

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("getcwd");
        exit(EXIT_FAILURE);
    }

    if (getenv("PATH") == NULL) {
        setenv("PATH", "/bin:/usr/bin", 1);
    }

    memset(&bg_job, 0, sizeof(bg_job));

    while (1) {
        check_background_job();
        
        printf("sshell@ucd$ ");
        fflush(stdout);

        if (!fgets(cmdline, CMDLINE_MAX, stdin)) {
            if (feof(stdin)) {
                fprintf(stderr, "Error: end of file\n");
                exit(EXIT_SUCCESS);
            } else {
                fprintf(stderr, "Error: failed to read command\n");
                exit(EXIT_FAILURE);
            }
        }

        if (!isatty(STDIN_FILENO)) {
            printf("%s", cmdline);
            fflush(stdout);
        }

        size_t len = strlen(cmdline);
        if (len > 0 && cmdline[len - 1] == '\n') {
            cmdline[len - 1] = '\0';
        }

        if (strlen(cmdline) == 0) {
            continue;
        }

        char original_cmd[CMDLINE_MAX];
        strncpy(original_cmd, cmdline, CMDLINE_MAX);

        Job job;
        memset(&job, 0, sizeof(job));
        strncpy(job.cmdline, cmdline, CMDLINE_MAX - 1);
        job.cmdline[CMDLINE_MAX - 1] = '\0';
        
        parse_command_line(cmdline, &job);

        if (job.command_count > 0) {
            execute_job(&job);

            if (job.background) {
                if (have_bg_job) {
                    free_job(&bg_job);
                }
                
                memcpy(&bg_job, &job, sizeof(Job));
                have_bg_job = 1;
            } else {
                free_job(&job);
            }
        }
    }

    return EXIT_SUCCESS;
}

void check_background_job(void)
{
    if (have_bg_job) {
        int status;
        int all_done = 1;
        int statuses[MAX_COMMANDS] = {0};

        for (int i = 0; i < bg_job.command_count; i++) {
            if (bg_job.pids[i] > 0) {
                pid_t result = waitpid(bg_job.pids[i], &status, WNOHANG);
                if (result == 0) {
                    all_done = 0;
                } else if (result > 0) {
                    bg_job.pids[i] = -1;
                    if (WIFEXITED(status)) {
                        statuses[i] = WEXITSTATUS(status);
                    } else {
                        statuses[i] = 1;
                    }
                } else {
                    if (errno != ECHILD) {
                        perror("waitpid");
                    }
                    bg_job.pids[i] = -1;
                    statuses[i] = 1;
                }
            }
        }

        if (all_done) {
            fprintf(stderr, "+ completed '%s' ", bg_job.cmdline);
            for (int i = 0; i < bg_job.command_count; i++) {
                fprintf(stderr, "[%d]", statuses[i]);
            }
            fprintf(stderr, "\n");
            fflush(stderr);

            free_job(&bg_job);
            have_bg_job = 0;
        }
    }
}

void free_job(Job *job)
{
    for (int i = 0; i < job->command_count; i++) {
        Command *cmd = &job->commands[i];
        
        for (int j = 0; j < cmd->argc; j++) {
            if (cmd->argv[j] != NULL) {
                free(cmd->argv[j]);
                cmd->argv[j] = NULL;
            }
        }
        
        if (cmd->output_file != NULL) {
            free(cmd->output_file);
            cmd->output_file = NULL;
        }
        if (cmd->input_file != NULL) {
            free(cmd->input_file);
            cmd->input_file = NULL;
        }
    }
    
    job->command_count = 0;
    job->background = 0;
    job->completed = 0;
    memset(job->cmdline, 0, CMDLINE_MAX);
    memset(job->pids, 0, sizeof(job->pids));
}

void parse_command_line(char *cmdline, Job *job)
{
    char *token;
    int cmd_index = 0;
    int arg_index = 0;
    int parsing_output = 0;
    int parsing_input = 0;
    char *cmd_copy = strdup(cmdline);  
    
    if (cmd_copy == NULL) {
        perror("strdup");
        exit(EXIT_FAILURE);
    }

    size_t len = strlen(cmd_copy);
    if (len > 0 && cmd_copy[len-1] == '&') {
        job->background = 1;
        cmd_copy[len-1] = '\0';
        
        len = strlen(cmd_copy);
        while (len > 0 && (cmd_copy[len-1] == ' ' || cmd_copy[len-1] == '\t')) {
            cmd_copy[--len] = '\0';
        }
    }

    token = strtok(cmd_copy, " \t");
    while (token != NULL && cmd_index < MAX_COMMANDS) {
        if (strcmp(token, "|") == 0) {
            if (arg_index == 0) {
                fprintf(stderr, "Error: missing command\n");
                job->command_count = 0;
                free_job(job);
                free(cmd_copy);
                return;
            }

            if (job->commands[cmd_index].output_file != NULL) {
                fprintf(stderr, "Error: mislocated output redirection\n");
                job->command_count = 0;
                free_job(job);
                free(cmd_copy);
                return;
            }

            if (cmd_index > 0 && job->commands[cmd_index].input_file != NULL) {
                fprintf(stderr, "Error: mislocated input redirection\n");
                job->command_count = 0;
                free_job(job);
                free(cmd_copy);
                return;
            }

            job->commands[cmd_index].argv[arg_index] = NULL;
            job->commands[cmd_index].argc = arg_index;

            cmd_index++;
            arg_index = 0;
            parsing_output = 0;
            parsing_input = 0;
        }
        else if (strcmp(token, ">") == 0) {
            if (arg_index == 0) {
                fprintf(stderr, "Error: missing command\n");
                job->command_count = 0;
                free_job(job);
                free(cmd_copy);
                return;
            }

            job->commands[cmd_index].argv[arg_index] = NULL;
            job->commands[cmd_index].argc = arg_index;

            parsing_output = 1;
        }
        else if (strcmp(token, "<") == 0) {
            if (arg_index == 0) {
                fprintf(stderr, "Error: missing command\n");
                job->command_count = 0;
                free_job(job);
                free(cmd_copy);
                return;
            }

            if (cmd_index > 0) {
                fprintf(stderr, "Error: mislocated input redirection\n");
                job->command_count = 0;
                free_job(job);
                free(cmd_copy);
                return;
            }

            job->commands[cmd_index].argv[arg_index] = NULL;
            job->commands[cmd_index].argc = arg_index;

            parsing_input = 1;
        }
        else if (strcmp(token, "&") == 0) {
            char *next_token = strtok(NULL, " \t");
            if (next_token != NULL) {
                fprintf(stderr, "Error: mislocated background sign\n");
                job->command_count = 0;
                free_job(job);
                free(cmd_copy);
                return;
            }

            job->background = 1;
            
            job->commands[cmd_index].argv[arg_index] = NULL;
            job->commands[cmd_index].argc = arg_index;
        }
        else if (parsing_output) {
            job->commands[cmd_index].output_file = strdup(token);
            parsing_output = 0;
            
            int fd = open(token, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd == -1) {
                fprintf(stderr, "Error: cannot open output file\n");
                job->command_count = 0;
                free_job(job);
                free(cmd_copy);
                return;
            }
            close(fd);
        }
        else if (parsing_input) {
            job->commands[cmd_index].input_file = strdup(token);
            parsing_input = 0;
            
            int fd = open(token, O_RDONLY);
            if (fd == -1) {
                fprintf(stderr, "Error: cannot open input file\n");
                job->command_count = 0;
                free_job(job);
                free(cmd_copy);
                return;
            }
            close(fd);
        }
        else {
            if (arg_index >= ARGUMENT_MAX) {
                fprintf(stderr, "Error: too many process arguments\n");
                job->command_count = 0;
                free_job(job);
                free(cmd_copy);
                return;
            }

            job->commands[cmd_index].argv[arg_index] = strdup(token);
            arg_index++;
        }

        token = strtok(NULL, " \t");
    }

    if (parsing_output) {
        fprintf(stderr, "Error: no output file\n");
        job->command_count = 0;
        free_job(job);
        free(cmd_copy);
        return;
    }

    if (parsing_input) {
        fprintf(stderr, "Error: no input file\n");
        job->command_count = 0;
        free_job(job);
        free(cmd_copy);
        return;
    }

    if (arg_index == 0 && cmd_index > 0) {
        fprintf(stderr, "Error: missing command\n");
        job->command_count = 0;
        free_job(job);
        free(cmd_copy);
        return;
    }

    if (arg_index > 0) {
        job->commands[cmd_index].argv[arg_index] = NULL;
        job->commands[cmd_index].argc = arg_index;
        job->command_count = cmd_index + 1;
    }

    free(cmd_copy);
}

int execute_builtin(Command *cmd, const char *cmdline)
{
    if (strcmp(cmd->argv[0], "exit") == 0) {
        if (have_bg_job) {
            fprintf(stderr, "Error: active job still running\n");
            fprintf(stderr, "+ completed 'exit' [1]\n");
            return 1;
        }
        
        fprintf(stderr, "Bye...\n");
        fprintf(stderr, "+ completed 'exit' [0]\n");
        exit(EXIT_SUCCESS);
    }
    else if (strcmp(cmd->argv[0], "cd") == 0) {
        const char *dir = cmd->argc > 1 ? cmd->argv[1] : getenv("HOME");
        if (dir == NULL) {
            fprintf(stderr, "Error: HOME environment variable not set\n");
            fprintf(stderr, "+ completed 'cd' [1]\n");
            return 1;
        }
        if (chdir(dir) != 0) {
            fprintf(stderr, "Error: cannot cd into directory\n");
            fprintf(stderr, "+ completed '%s' [1]\n", cmdline);
            return 1;
        }
        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            perror("getcwd");
            exit(EXIT_FAILURE);
        }
        fprintf(stderr, "+ completed '%s' [0]\n", cmdline);
        return 0;
    }
    else if (strcmp(cmd->argv[0], "pwd") == 0) {
        printf("%s\n", cwd);
        fflush(stdout);
        fprintf(stderr, "+ completed '%s' [0]\n", cmdline);
        return 0;
    }

    return -1;
}

int execute_job(Job *job)
{
    int status[MAX_COMMANDS] = {0};
    int pipefd[MAX_COMMANDS-1][2];  

    if (job->command_count == 1 && !job->background) {
        Command *cmd = &job->commands[0];
        int builtin_status = execute_builtin(cmd, job->cmdline);
        if (builtin_status >= 0) {
            return builtin_status;
        }
    }

    for (int i = 0; i < job->command_count - 1; i++) {
        if (pipe(pipefd[i]) == -1) {
            perror("pipe");
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < job->command_count; i++) {
        Command *cmd = &job->commands[i];

        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            exit(EXIT_FAILURE);
        }

        if (pid == 0) {
            if (cmd->input_file != NULL) {
                int fd = open(cmd->input_file, O_RDONLY);
                if (fd == -1) {
                    fprintf(stderr, "Error: cannot open input file\n");
                    exit(EXIT_FAILURE);
                }
                if (dup2(fd, STDIN_FILENO) == -1) {
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }
                close(fd);
            } 
            else if (i > 0) {
                if (dup2(pipefd[i-1][0], STDIN_FILENO) == -1) {
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }
            }

            if (cmd->output_file != NULL) {
                int fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd == -1) {
                    fprintf(stderr, "Error: cannot open output file\n");
                    exit(EXIT_FAILURE);
                }
                if (dup2(fd, STDOUT_FILENO) == -1) {
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }
                close(fd);
            } 
            else if (i < job->command_count - 1) {
                if (dup2(pipefd[i][1], STDOUT_FILENO) == -1) {
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }
            }

            for (int j = 0; j < job->command_count - 1; j++) {
                close(pipefd[j][0]);
                close(pipefd[j][1]);
            }

            execvp(cmd->argv[0], cmd->argv);
            
            fprintf(stderr, "Error: command not found\n");
            exit(127); 
        } else {
            job->pids[i] = pid;
        }
    }

    for (int i = 0; i < job->command_count - 1; i++) {
        close(pipefd[i][0]);
        close(pipefd[i][1]);
    }

    if (!job->background) {
        for (int i = 0; i < job->command_count; i++) {
            int cmd_status;
            waitpid(job->pids[i], &cmd_status, 0);
            if (WIFEXITED(cmd_status)) {
                status[i] = WEXITSTATUS(cmd_status);
            } else {
                status[i] = 1; 
            }
        }

        fprintf(stderr, "+ completed '%s' ", job->cmdline);
        for (int i = 0; i < job->command_count; i++) {
            fprintf(stderr, "[%d]", status[i]);
        }
        fprintf(stderr, "\n");
    }

    return 0;
}