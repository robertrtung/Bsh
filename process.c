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
#define errorExit(status) perror("pipe"), exit(status)

CMD **extractPipeChain(CMD *cmd, int *size, int *count);

int processStage(CMD *cmd, int *backgrounded);

int processPipe(CMD *cmd, int *backgrounded);

int processHelper(CMD *cmd, int *backgrounded);

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
		while((*size) <= (*count)){
			(*size) *= 2;
			pipeChain = realloc(pipeChain,sizeof(CMD *)*(*size));
		}
		pipeChain[(*count)] = cmd->right;
		(*count)++;
	} else if((cmd->type == SIMPLE) || (cmd->type == SUBCMD)){
		pipeChain = malloc(sizeof(CMD *)*(*size));
		pipeChain[0] = cmd;
		assert((*count) == 0);
		(*count)++;
	} else{
		perror("pipe");
		return 0;
	}
	return pipeChain;
}

int processPipe(CMD *cmd, int *backgrounded){
	if(cmd->type != PIPE){
		fprintf(stderr,"Entered wrong function!\n");
		return errno;
	}
	int chainSize = 1;
	int numPiped = 0;
	CMD **pipeChain = extractPipeChain(cmd,&chainSize,&numPiped);
	pipeChain = realloc(pipeChain,sizeof(CMD *)*(numPiped));

	struct entry{
		int pid, status;
	};
	struct entry *table = (struct entry *) calloc(numPiped,sizeof(struct entry));

	int *fd = malloc(sizeof(int)*2);
	pid_t pid;
	int status;
	int fdin;
	int i;
	int j;

	if(numPiped < 2){
		fprintf(stderr,"Usage: piping less than 2 streams");
		free(fd);
		free(pipeChain);
		free(table);
		exit(0);
	}

	fdin = 0;
	for(i=0;i < numPiped - 1;i++){
		if(pipe(fd) || (pid = fork()) < 0){
			errorExit(EXIT_FAILURE);
		} else if(pid == 0){
			//child
			close(fd[0]);
			if(fdin != 0){
				dup2(fdin,0);
				close(fdin);
			}

			if(fd[1] != 1){
				dup2(fd[1],1);
				close(fd[1]);
			}
			if(pipeChain[i] != 0){
				processHelper(pipeChain[i],backgrounded);
			} else{
				perror("pipeChain");
				free(fd);
				free(pipeChain);
				free(table);
				return errno;
			}
			exit(EXIT_SUCCESS);
		} else{
			table[i].pid = pid;
			if(i > 0){
				close(fdin);
			}
			fdin = fd[0];
			close(fd[1]);
		}
	}

	if((pid = fork()) < 0){
		errorExit(EXIT_FAILURE);
	} else if(pid == 0){
		if(fdin != 0){
			dup2(fdin,0);
			close(fdin);
		}
		if(pipeChain[numPiped-1] != 0){
			processHelper(pipeChain[numPiped-1],backgrounded);
		} else{
			perror("pipeChain");
			free(fd);
			free(pipeChain);
			free(table);
			return errno;
		}
		exit(EXIT_SUCCESS);
	} else{
		table[numPiped-1].pid = pid;
		if(i > 0){
			close(fdin);
		}
	}

	for(i=0;i<numPiped;){
		pid = wait(&status);
		for(j=0;(j<numPiped) && (table[j].pid != pid);j++){
		}
		if(j < numPiped){
			table[j].status = status;
			i++;
		}
	}

	free(fd);
	free(pipeChain);
	free(table);

	return EXIT_SUCCESS;
}

int processStage(CMD *cmd, int *backgrounded){
	int rightNoBack = (*backgrounded);
	int to;
	int from;
	if(cmd->toType == RED_OUT){
		to = open(cmd->toFile,O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
	} else if(cmd->toType == RED_OUT_APP){
		to = open(cmd->toFile,O_APPEND | O_CREAT | O_RDWR, S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
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
		leftStatus = processHelper(cmd->left,backgrounded);
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
						if(to != STDO){
							close(to);
						}
						if(from != STDI){
							close(from);
						}
						return errno;
					}
					if(dup2(from,STDI) == -1){
						perror("dup2");
						if(to != STDO){
							close(to);
						}
						if(from != STDI){
							close(from);
						}
						return errno;
					}
					for(int i=0;i<cmd->nLocal;i++){
						if(setenv(cmd->locVar[i],cmd->locVal[i],1) != 0){
							perror("setenv");
							if(to != STDO){
								close(to);
							}
							if(from != STDI){
								close(from);
							}
							return errno;
						}
					}
					status = execvp(cmd->argv[0],cmd->argv);
					if(status < 0){
						perror("execvp");
						if(to != STDO){
							close(to);
						}
						if(from != STDI){
							close(from);
						}
						return errno;
					}
					for(int i=0;i<cmd->nLocal;i++){
						if(unsetenv(cmd->locVar[i]) != 0){
							perror("setenv");
							if(to != STDO){
								close(to);
							}
							if(from != STDI){
								close(from);
							}
							return errno;
						}
					}
				} else if(pid < 0){
					perror("fork");
					if(to != STDO){
						close(to);
					}
					if(from != STDI){
						close(from);
					}
					return errno;
				} else{
					//parent
					if((leftStatus != 1) && (!(*backgrounded))){
						waitpid(pid, &status, 0);
					} else if (leftStatus != 1){
						fprintf(stderr,"Backgrounded: %d\n",pid);
						waitpid(pid, &status, WNOHANG);
						fprintf(stderr,"Completed: %d (%d)\n",pid,status);
					}
				}
			}
			break;
		case SUBCMD:
			secondFlag = 0;
			break;
		default:
			fprintf(stderr,"Entered wrong function!\n");
			break;
	}
	if((cmd->right != 0) && secondFlag){
		processHelper(cmd->right,&rightNoBack);
	}
	if(to != STDO){
		close(to);
	}
	if(from != STDI){
		close(from);
	}
	return status;
}

int processHelper(CMD *cmd, int *backgrounded){
	int rightNoBack = (*backgrounded);
	if(cmd == 0){
		return 1;
	}
	int leftStatus = 0;
	int status = 0;
	int secondFlag = 1;
	if(cmd->type == SEP_BG){
		(*backgrounded) = 1;
	}
	if(cmd->type != PIPE){
		if(cmd->left != 0){
			leftStatus = processHelper(cmd->left,backgrounded);
		}

		switch(cmd->type){
			case SIMPLE:
				status = processStage(cmd,backgrounded);
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
				break;
			case SUBCMD:
				status = processStage(cmd,backgrounded);
				break;
			case NONE:
				printf("NONE\n");
				break;
			default:
				break;
		}
		if((cmd->right != 0) && secondFlag){
			processHelper(cmd->right,&rightNoBack);
		}
	} else{
		//pipe
		processPipe(cmd,backgrounded);
	}
	return status;
}

int process(CMD *cmd){
	int backgrounded = 0;
	processHelper(cmd,&backgrounded);
	return 1;
}