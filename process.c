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
#include <assert.h>

#define BG (1)
#define STDI (0)
#define STDO (1)
#define EXIT_STATUS(x) (WIFEXITED(x) ? WEXITSTATUS(x) : 128+WTERMSIG(x))

CMD **extractPipeChain(CMD *cmd, int *size, int *count);

int processStage(CMD *cmd);

int processHelper(CMD *cmd);

int process(CMD *cmd);

//TODO: implement signals
//TODO: fix status updates
//TODO: reap zombies periodically
//TODO: Free all allocated storage

CMD **extractPipeChain(CMD *cmd, int *size, int *count){
	//extract the array of pointers to cmd's that need to be piped together
	CMD **pipeChain;
	if(cmd->type == PIPE){
		pipeChain = extractPipeChain(cmd->left,size,count);
	} else if((cmd->type == SIMPLE) || (cmd->type == SUBCMD)){
		pipeChain = malloc(sizeof(CMD *)*(*size));
		pipeChain[0] = cmd->left;
		assert((*count) == 0);
		(*count)++;
	} else{
		perror("pipe");
		return 0;
	}
	while((*size) <= (*count)){
		(*size) *= 2;
		pipeChain = realloc(pipeChain,sizeof(CMD *)*(*size));
	}
	pipeChain[(*count)] = cmd->right;
	(*count)++;
	return pipeChain;
}

int processPipe(CMD *cmd){
	if(cmd->type != PIPE){
		fprintf(stderr,"Entered wrong function!\n");
		return errno;
	}
	int chainSize = 0;
	int numPiped = 0;
	CMD **pipeChain = extractPipeChain(cmd,&chainSize,&numPiped);
	pipeChain = realloc(pipeChain,sizeof(CMD *)*(numPiped));

	//TODO: use pipe.c on pipeChain

	int *filedes = malloc(sizeof(int)*2);
	if(pipe(filedes)==0){
		//do pipe stuff
		if(cmd->left != 0){
			switch(cmd->left->type){
				case SIMPLE:
					//TODO
					break;
				case SUBCMD:
					//TODO
					break;
				case PIPE:
					//TODO
					break;
				default:
					perror("pipe");
					free(filedes);
					free(pipeChain);
					return errno;
					break;
			}
		} else{
			perror("Tree format");
			free(filedes);
			free(pipeChain);
			return errno;
		}
	} else{
		perror("pipe");
		free(filedes);
		free(pipeChain);
		return errno;
	}
	free(filedes);
	free(pipeChain);
	//TODO: return value for this
	return 1;
}
int processStage(CMD *cmd){
	int to;
	int from;
	if((cmd->toType == RED_OUT) || (cmd->toType == RED_OUT_APP)){
		to = open(cmd->toFile,O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
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
	if(cmd->left != 0){
		leftStatus = processHelper(cmd->left);
	}
	switch(cmd->type){
		case SIMPLE:
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
				pid = fork();
				if(pid == 0){
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
				} else if(pid < 0){
					perror("fork");
					return errno;
				} else{
					//parent
					if(leftStatus != 1){
						waitpid(pid, &status, 0);
					}
				}
			}
			break;
		case SUBCMD:
			printf("PIPE\n");
			break;
		default:
			fprintf(stderr,"Entered wrong function!\n");
			break;
	}
	if((cmd->right != 0) && secondFlag){
		processHelper(cmd->right);
	}
	if(to != STDO){
		close(to);
	}
	if(from != STDI){
		close(from);
	}
	return status;
}

int processHelper(CMD *cmd){
	int leftStatus = 0;
	int status = 0;
	int secondFlag = 1;
	if(cmd->type != PIPE){
		if(cmd->left != 0){
			leftStatus = processHelper(cmd->left);
		}

		switch(cmd->type){
			case SIMPLE:
				status = processStage(cmd);
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
				status = processStage(cmd);
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
		processPipe(cmd);
	}
	return status;
}

int process(CMD *cmd){
	processHelper(cmd);
	return 1;
}