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
#define MY_ERR (1)

CMD **extractPipeChain(CMD *cmd, int *size, int *count);

int processPipe(CMD *cmd, int *backgrounded, int oldTo, int oldFrom);

int openIO(CMD *cmd, int *to, int *from, int oldTo, int oldFrom);

void closeIO(int to, int from, int oldTo, int oldFrom);

void setLocal(CMD *cmd, int to, int from,int oldTo, int oldFrom);

void unsetLocal(CMD *cmd, int to, int from,int oldTo, int oldFrom);

int processStage(CMD *cmd, int *backgrounded, int oldTo, int oldFrom);

int processSub(CMD *cmd, int *backgrounded, int oldTo, int oldFrom);

int processHelper(CMD *cmd, int *backgrounded, int oldTo, int oldFrom);

int process(CMD *cmd);

//TODO: implement signals

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

int processPipe(CMD *cmd, int *backgrounded,int oldTo, int oldFrom){
	if(cmd->type != PIPE){
		fprintf(stderr,"Entered wrong function!\n");
		return MY_ERR;
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
	int outStatus = 0;
	int tempStatus = 0;

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
			free(fd);
			free(pipeChain);
			free(table);
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
				tempStatus = processHelper(pipeChain[i],backgrounded,oldTo,oldFrom);
			} else{
				perror("pipeChain");
				free(fd);
				free(pipeChain);
				free(table);
				return MY_ERR;
			}
			free(fd);
			free(pipeChain);
			free(table);
			exit(tempStatus);
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
		free(fd);
		free(pipeChain);
		free(table);
		errorExit(EXIT_FAILURE);
	} else if(pid == 0){
		if(fdin != 0){
			dup2(fdin,0);
			close(fdin);
		}
		if(pipeChain[numPiped-1] != 0){
			tempStatus = processHelper(pipeChain[numPiped-1],backgrounded,oldTo,oldFrom);
		} else{
			perror("pipeChain");
			free(fd);
			free(pipeChain);
			free(table);
			return MY_ERR;
		}
		free(fd);
		free(pipeChain);
		free(table);
		exit(tempStatus);
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

	for(i=0;i<numPiped;i++){
		if(table[i].status) {
			outStatus = table[i].status;
		}
	}

	free(fd);
	free(pipeChain);
	free(table);

	char toSet[256];
	sprintf(toSet,"%d",outStatus);
	setenv("?",toSet,1);

	return EXIT_STATUS(outStatus);
}

int openIO(CMD *cmd, int *to, int *from, int oldTo, int oldFrom){
	if(cmd->toType == RED_OUT){
		if((*to) != STDO){
			close((*to));
		}
		(*to) = open(cmd->toFile,O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
		if((*to) < 0){
			perror("open");
			return errno;
		}
	} else if(cmd->toType == RED_OUT_APP){
		if((*to) != STDO){
			close((*to));
		}
		(*to) = open(cmd->toFile,O_APPEND | O_CREAT | O_RDWR, S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
		if((*to) < 0){
			perror("open");
			return errno;
		}
	} else{
		(*to) = oldTo;
	}
	if(cmd->fromType == RED_IN){
		if((*from) != STDI){
			close((*from));
		}
		(*from) = open(cmd->fromFile,O_RDONLY);
		if((*from) < 0){
			perror("open");
			return errno;
		}
	} else{
		(*from) = oldFrom;
	}
	return 0;
}

void closeIO(int to, int from,int oldTo, int oldFrom){
	if((to != STDO) && (oldTo == STDO)){
		close(to);
	}
	if((from != STDI) && (oldFrom == STDI)){
		close(from);
	}
}

void setLocal(CMD *cmd, int to, int from, int oldTo, int oldFrom){
	for(int i=0;i<cmd->nLocal;i++){
		if(setenv(cmd->locVar[i],cmd->locVal[i],1) != 0){
			perror("setenv");
			closeIO(to,from,oldTo,oldFrom);
			exit(errno);
			//return errno;
		}
	}
}

void unsetLocal(CMD *cmd, int to, int from, int oldTo, int oldFrom){
	for(int i=0;i<cmd->nLocal;i++){
		if(unsetenv(cmd->locVar[i]) != 0){
			perror("setenv");
			closeIO(to,from,oldTo,oldFrom);
			exit(errno);
			//return errno;
		}
	}
}

int processStage(CMD *cmd, int *backgrounded,int oldTo, int oldFrom){
	int rightNoBack = (*backgrounded);
	int to;
	int from;
	if(openIO(cmd,&to,&from,oldTo,oldFrom) < 0){
		closeIO(to,from,oldTo,oldFrom);
		return errno;
	}
	int leftStatus = 0;
	pid_t pid;
	int status = 0;
	int secondFlag = 1;
	if((cmd->left != 0) && (cmd->type != SUBCMD)){
		leftStatus = processHelper(cmd->left,backgrounded,oldTo,oldFrom);
	}
	int reapedPid;
	switch(cmd->type){
		case SIMPLE:
			if(strcmp(cmd->argv[0],"cd") == 0){
				if(cmd->argc < 2){
					if(chdir(getenv("HOME")) == -1){
						perror("chdir");
						closeIO(to,from,oldTo,oldFrom);
						return errno;
					}
				} else{
					if(chdir(cmd->argv[1]) == -1){
						perror("chdir");
						closeIO(to,from,oldTo,oldFrom);
						return errno;
					}
				}
			} else if(strcmp(cmd->argv[0],"dirs") == 0){
				char *buffer = malloc(sizeof(char)*(PATH_MAX + 1));
				if(getcwd(buffer,PATH_MAX + 1) == 0){
					free(buffer);
					perror("getcwd");
					closeIO(to,from,oldTo,oldFrom);
					return errno;
				}
				printf("%s\n",getcwd(buffer,PATH_MAX + 1));
				free(buffer);
			} else if(strcmp(cmd->argv[0],"wait") == 0){
				while((reapedPid = waitpid((pid_t)-1, &status, 0)) != -1){
					if(reapedPid != 0){
						fprintf(stderr,"Completed: %d (%d)\n",reapedPid,status);
					}
				}
			}else{
				pid = fork();
				if(pid == 0){
					//child
					if(dup2(to,STDO) == -1){
						perror("dup21");
						closeIO(to,from,oldTo,oldFrom);
						exit(errno);
						//return errno;
					}
					if(dup2(from,STDI) == -1){
						perror("dup22");
						closeIO(to,from,oldTo,oldFrom);
						exit(errno);
						//return errno;
					}
					setLocal(cmd,to,from,oldTo,oldFrom);
					status = execvp(cmd->argv[0],cmd->argv);
					if(status < 0){
						perror("execvp");
						closeIO(to,from,oldTo,oldFrom);
						exit(errno);
						//return errno;
					}
					unsetLocal(cmd,to,from,oldTo,oldFrom);
					exit(status);
				} else if(pid < 0){
					perror("fork");
					closeIO(to,from,oldTo,oldFrom);
					return errno;
				} else{
					//parent
					if((leftStatus != 1) && (!(*backgrounded))){
						waitpid(pid, &status, 0);
					} else if (leftStatus != 1){
						fprintf(stderr,"Backgrounded: %d\n",pid);
					}
				}
			}
			break;
		case SUBCMD:
			processSub(cmd,backgrounded,oldTo,oldFrom);
			secondFlag = 0;
			break;
		default:
			fprintf(stderr,"Entered wrong function!\n");
			break;
	}
	if((cmd->right != 0) && secondFlag){
		processHelper(cmd->right,&rightNoBack,oldTo,oldFrom);
	}
	closeIO(to,from,oldTo,oldFrom);
	return EXIT_STATUS(status);
}

int processSub(CMD *cmd, int *backgrounded, int oldTo, int oldFrom){
	pid_t pid;
	int status = 0;
	int to;
	int from;
	if(openIO(cmd,&to,&from,oldTo,oldFrom) < 0){
		closeIO(to,from,oldTo,oldFrom);
		return errno;
	}
	if(cmd->type != SUBCMD){
		fprintf(stderr,"In wrong function!\n");
		return MY_ERR;
	}
	pid = fork();
	if(pid == 0){
		//child
		status = processHelper(cmd->left,backgrounded,to,from);
		if(dup2(to,STDO) == -1){
			perror("dup23");
			closeIO(to,from,oldTo,oldFrom);
			exit(errno);
			//return errno;
		}
		if(dup2(from,STDI) == -1){
			perror("dup24");
			closeIO(to,from,oldTo,oldFrom);
			exit(errno);
			//return errno;
		}
		exit(status);
	} else if(pid < 0){
		perror("fork");
		closeIO(to,from,oldTo,oldFrom);
		return errno;
	} else{
		//parent
		if(!(*backgrounded)){
			waitpid(pid, &status, 0);
		} else{
			fprintf(stderr,"Backgrounded: %d\n",pid);
		}
	}
	closeIO(to,from,STDO,STDI);
	return status;
}

int processHelper(CMD *cmd, int *backgrounded,int oldTo, int oldFrom){
	if(cmd->type == NONE){
		return 1;
	}
	setLocal(cmd,STDO,STDI,STDO,STDI);
	int status = 0;
	int reapedPid;
	while(((reapedPid = waitpid((pid_t)-1, &status, WNOHANG)) != -1) && (reapedPid != 0)){
		fprintf(stderr,"Completed: %d (%d)\n",reapedPid,status);
	}
	int rightNoBack = (*backgrounded);
	if(cmd == 0){
		return 1;
	}
	status = 0;
	int statusChanged = 0;
	int endLeft = 0;
	int leftStatus = 0;
	int rightStatus = 0;
	int secondFlag = 1;
	if(cmd->type == SEP_BG){
		(*backgrounded) = 1;
	}
	if(cmd->type != PIPE){
		if((cmd->left != 0) && (cmd->type != SUBCMD)){
			if((cmd->type != SEP_END) && (cmd->type != SEP_AND) && (cmd->type != SEP_OR)){
				leftStatus = processHelper(cmd->left,backgrounded,oldTo,oldFrom);
			} else{
				leftStatus = processHelper(cmd->left,&endLeft,oldTo,oldFrom);
			}
		}

		switch(cmd->type){
			case SIMPLE:
				status = processStage(cmd,backgrounded,oldTo,oldFrom);
				if(!(*backgrounded)){
					statusChanged = 1;
				}
				break;
			case PIPE:
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
				status = processStage(cmd,backgrounded,oldTo,oldFrom);
				if(!(*backgrounded)){
					statusChanged = 1;
				}
				break;
			case NONE:
				break;
			default:
				break;
		}
		if((cmd->right != 0) && secondFlag){
			rightStatus = processHelper(cmd->right,&rightNoBack,oldTo,oldFrom);
		}
	} else{
		//pipe
		status = processPipe(cmd,backgrounded,oldTo,oldFrom);
		if(!(*backgrounded)){
			statusChanged = 1;
		}
	}
	if(statusChanged){
		char toSet[256];
		sprintf(toSet,"%d",status);
		setenv("?",toSet,1);
	}
	if(leftStatus != 0){
		status = leftStatus;
	}
	if(rightStatus != 0){
		status = rightStatus;
	}
	unsetLocal(cmd,STDO,STDI,STDO,STDI);
	return status;
}

int process(CMD *cmd){
	int backgrounded = 0;
	processHelper(cmd,&backgrounded,STDO,STDI);
	return 1;
}