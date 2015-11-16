/*
 * Bsh
 * 
 * by: Robert Tung
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include "/c/cs323/Hwk5/process-stub.h"
#include <sys/types.h>

#define BG (1)
#define STDI (0)
#define STDO (1)
#define EXIT_STATUS(x) (WIFEXITED(x) ? WEXITSTATUS(x) : 128+WTERMSIG(x))

//TODO: implement signals
//TODO: fix status updates
//TODO: reap zombies periodically
//TODO: Free all allocated storage
//


int processHelper(CMD *cmd){
	int to;
	int from;
	if((cmd->toType == RED_OUT) || (cmd->toType == RED_OUT_APP)){
		to = open(cmd->toFile,O_WRONLY);
	} else{
		to = STDO;
	}
	if(cmd->fromType == RED_IN){
		from = open(cmd->fromFile,O_RDONLY);
	} else{
		from = STDI;
	}
	int leftStatus = 0;
	pid_t pid;
	int status = 0;
	int secondFlag = 1;
	if(cmd->type != PIPE){
		if(cmd->left != 0){
			leftStatus = processHelper(cmd->left);
		}

		switch(cmd->type){
			case SIMPLE:
				pid = fork();
				if(pid == 0){
					if(strcmp(cmd->argv[0],"cd") == 0){
						if(cmd->argc < 2){
							chdir(getenv("HOME"));
						} else{
							chdir(cmd->argv[1]);
						}
					} else if(strcmp(cmd->argv[0],"dirs") == 0){
						char *buffer = malloc(sizeof(char)*(PATH_MAX + 1));
						printf("%s\n",getcwd(buffer,PATH_MAX + 1));
						free(buffer);
					} else{
						//child
						if(dup2(to,STDO) == -1){
							perror("dup2");
							return errno;
						}
						if(dup2(from,STDI) == -1){
							perror("dup2");
							return errno;
						}
						for(int i=0;i<cmd->nLocal;i++){
							if(setenv(cmd->locVar[i],cmd->locVal[i],1) != 0){
								perror("setenv");
								return errno;
							}
						}
						status = execvp(cmd->argv[0],cmd->argv);
						if(status < 0){
							perror("execvp");
							return errno;
						}
						for(int i=0;i<cmd->nLocal;i++){
							if(unsetenv(cmd->locVar[i]) != 0){
								perror("setenv");
								return errno;
							}
						}
					}
				} else if(pid < 0){
					perror("fork");
					return errno;
				} else{
					//parent
					if(leftStatus != 1){
						waitpid(pid, &status, 0);
					}
				}
				break;
			case PIPE:
				printf("PIPE\n");
				break;
			case SEP_AND:
				if(leftStatus != 0){
					secondFlag = 0;
				}
				break;
			case SEP_OR:
				if(leftStatus == 0){
					secondFlag = 0;
				}
				break;
			case SEP_END:
				break;
			case SEP_BG:
				//TODO: Don't forget to reap zombie processes
				return BG;
				//printf("SEP_BG\n");
				//fprintf(stderr,"Backgrounded: %d\n",pid);
				break;
			case SUBCMD:
				secondFlag = 0;
				break;
			case NONE:
				printf("NONE\n");
				break;
			default:
				break;
		}
		if((cmd->right != 0) && secondFlag){
			processHelper(cmd->right);
		}
	} else{
		//pipe
		int *filedes = malloc(sizeof(int)*2);
		if(pipe(filedes)==0){
			//do pipe stuff
		} else{
			perror("pipe");
			return errno;
		}
		free(filedes);
	}

	if(to != STDO){
		close(to);
	}
	if(from != STDI){
		close(from);
	}
	return status;
}

int process(CMD *cmd){
	processHelper(cmd);
	return 1;
}