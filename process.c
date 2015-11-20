/*
 * Bsh
 * 
 * Bsh is a simple shell the handles local variables, 
 * simples commands, redirection of input and output,
 * pipelines, background commands, conjunction of commands,
 * groups of commands (subcommands), directory manipulation and more.
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

#define STDI (0) //file directory of stdin
#define STDO (1) //file directory of stdout
#define VARSIZE (256) //size allocated to store ?
#define SYS_ERR_FOUND (-1) //system call produced an error

//normalizes the exit status errno's and normal exit statuses
#define EXIT_STATUS(x) (WIFEXITED(x) ? WEXITSTATUS(x) : 128+WTERMSIG(x))

//used to exit from pipe when errors occur
//taken from Professor Eisenstat's pipe.c code
#define errorExit(status) perror("pipe"), exit(status)
#define MY_ERR (1) //used for errors that aren't system errors (no errno's)

/*
 * Handler for SIGINT (Ctrl-C) when not in a child process
 */
void sigint_handler_general(int sig);

/*
 * Handler for SIGINT (Ctrl-C) when in a child process
 */
void sigint_handler_child(int sig);

/*
 * Extract all the processes being piped together from the tree
 * cmd is the root of the tree, size is to put the size allocated
 * and count is to put the number of processes
 * Return extracted processes as array
 */
CMD **extractPipeChain(CMD *cmd, int *size, int *count);

/*
 * Performs the processing for pipelines (|)
 * Adapted from Professor Eisenstat's pipe.c code with modifications
 * cmd is the root of the tree holding the pipe, backgrounded is a flag
 * for whether this is happening in the backgorund, oldTo is the default
 * output if no new one is given, and oldFrom is the analogous input
 * Return status
 */
int processPipe(CMD *cmd, int *backgrounded, int oldTo, int oldFrom);

/*
 * Opens the input and output files given and set "to" and "from"
 * to the file descriptor of them
 * cmd is the root of the tree, to is the file descriptor of the output,
 * from is the file descriptor of the input, and oldTo and oldFrom are the
 * file descriptors of the output and input before this opening
 * Return 0 if no errors and errno if errors
 */
int openIO(CMD *cmd, int *to, int *from, int oldTo, int oldFrom);

/*
 * Close the input and output file descriptors
 * to is the output file descriptor to close, from is the input
 * file descriptor to close, and oldTo and oldFrom are the defaults before
 * these were set (the input and output of a subcommand rather than
 * the things within the subcommand if we are in a subcommand. otherwise
 * oldTo is STDO and oldFrom is STDI).
 */
void closeIO(int to, int from, int oldTo, int oldFrom);

/*
 * Set the local variables needed for the current command
 * cmd is the root of the tree, to is the output file descriptor,
 * from is the input file descriptor, oldTo and oldFrom are the default
 * output and input filedescriptors if to and from weren't set,
 * envNames and envValues store the old versions
 * of local variables that were overriden
 */
void setLocal(CMD *cmd, int to, int from, int oldTo, int oldFrom,
				char *envNames[], char *envValues[]);

/*
 * Unset the local variables. If setLocal replaced any defined variables,
 * put them back. cmd is the root of the tree,
 * to is the output file descriptor,
 * from is the input file descriptor, oldTo and oldFrom are the default
 * output and input filedescriptors if to and from weren't set,
 * envNames and envValues hold the old versions of
 * local variables that were overriden by setLocal
 */
void unsetLocal(CMD *cmd, int to, int from,int oldTo, int oldFrom,
				char *envNames[], char *envValues[]);

/*
 * Performs the processing of stages, including simple commands and subcommands
 * cmd is the root of the tree, backgrounded is a flag for whether it is run
 * in the background, and oldTo and oldFrom are the default output and input
 * Return status
 */
int processStage(CMD *cmd, int *backgrounded, int oldTo, int oldFrom);

/*
 * Performs the processing of subcommands
 * cmd is the root of the tree, backgrounded is a flag for whether it is run
 * in the background, and oldTo and oldFrom are the default output and input
 * Return status
 */
int processSub(CMD *cmd, int *backgrounded, int oldTo, int oldFrom);

/*
 * Recursive version of process. Performs the processing for all input
 * cmd is the root of the tree, backgrounded is a flag for whether it is run
 * in the background, and oldTo and oldFrom are the default output and input
 * Return status
 */
int processHelper(CMD *cmd, int *backgrounded, int oldTo, int oldFrom);

/*
 * Non-recursive version of process. Calls processHelper.
 * Called by rest of Professor Eisenstat's code.
 * cmd is the root of the tree being processed.
 * Return status
 */
int process(CMD *cmd);


void sigint_handler_general(int sig){
	//only exit if nothing left to wait for
	if(wait(0) == -1){
		printf("\n");
		exit(0);
	}
}

void sigint_handler_child(int sig){
	//children always exit when SIGINT given
	exit(0);
}

CMD **extractPipeChain(CMD *cmd, int *size, int *count){
	CMD **pipeChain;//stores the processes being pipes together
	if(cmd->type == PIPE){
		//root is a pipe so extract left and right
		pipeChain = extractPipeChain(cmd->left,size,count);
		while((*size) <= (*count)){//resize if necessary
			(*size) *= 2;
			pipeChain = realloc(pipeChain,sizeof(CMD *)*(*size));
		}
		//store the right
		pipeChain[(*count)] = cmd->right;
		(*count)++;
	} else if((cmd->type == SIMPLE) || (cmd->type == SUBCMD)){
		//no need to keep extracting recursively (at a leaf)
		//store the current node
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

int processPipe(CMD *cmd, int *backgrounded, int oldTo, int oldFrom){
	if(cmd->type != PIPE){//this function should only be used when a pipe
		fprintf(stderr,"Entered wrong function!\n");
		return MY_ERR;
	}
	int chainSize = 1;//size allocated for extracted processes in pipeline
	int numPiped = 0;//count of extracted processes in pipeline
	CMD **pipeChain = extractPipeChain(cmd,&chainSize,&numPiped);
	pipeChain = realloc(pipeChain,sizeof(CMD *)*(numPiped));

	struct entry{//table with (pid,status) for each child in pipeline
		int pid, status;
	};
	struct entry *table = (struct entry *) calloc(numPiped,
													sizeof(struct entry));

	int *fd = malloc(sizeof(int)*2);//read and write file descriptors for pipe
	pid_t pid; //process id of child
	int status; //status of child
	int fdin;//read end of last pipe (or original oldFrom)
	int i;
	int j;
	int outStatus = 0;//overall whether there was an error
	int tempStatus = 0;//status of current process

	if(numPiped < 2){
		fprintf(stderr,"Usage: piping less than 2 streams");
		free(fd);
		free(pipeChain);
		free(table);
		exit(0);
	}

	fdin = oldFrom;//original input stream
	for(i=0;i < numPiped - 1;i++){//create chain of processes
		if(pipe(fd) || (pid = fork()) < 0){
			free(fd);
			free(pipeChain);
			free(table);
			errorExit(EXIT_FAILURE);
		} else if(pid == 0){
			//child process
			signal(SIGINT, sigint_handler_child);
			close(fd[0]);//don't read from new pipe
			if(fdin != oldFrom){//set input to new pipe
				dup2(fdin,oldFrom);
				close(fdin);
			}

			if(fd[1] != oldTo){//set output to new pipe
				dup2(fd[1],oldTo);
				close(fd[1]);
			}
			if(pipeChain[i] != 0){//process current process in pipeline
				tempStatus = processHelper(pipeChain[i],
											backgrounded,
											oldTo,
											oldFrom);
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
			//parent process
			table[i].pid = pid;//store pid of child
			if(i > 0){//close last input stream if not stdin
				close(fdin);
			}
			fdin = fd[0];//remember read of new pipe
			close(fd[1]);//close last output stream
		}
	}

	if((pid = fork()) < 0){//create final process
		free(fd);
		free(pipeChain);
		free(table);
		errorExit(EXIT_FAILURE);
	} else if(pid == 0){
		//child process
		signal(SIGINT, sigint_handler_child);
		if(fdin != oldFrom){//set input to new pipe
			dup2(fdin,oldFrom);
			close(fdin);
		}
		if(pipeChain[numPiped-1] != 0){//process current process in pipeline
			tempStatus = processHelper(pipeChain[numPiped-1],
										backgrounded,
										oldTo,
										oldFrom);
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
		table[numPiped-1].pid = pid;//store pid of child
		if(i > 0){//close last input stream if not stdin
			close(fdin);
		}
	}

	for(i=0;i<numPiped;){//wait for all children to die
		pid = wait(&status);
		for(j=0;(j<numPiped) && (table[j].pid != pid);j++){
		}
		if(j < numPiped){//ignore zombie processes
			table[j].status = status;
			i++;
		}
	}

	for(i=0;i<numPiped;i++){//determine final output status
		if(table[i].status) {
			outStatus = table[i].status;
		}
	}

	free(fd);
	free(pipeChain);
	free(table);

	char toSet[VARSIZE];
	sprintf(toSet,"%d",outStatus);
	setenv("?",toSet,1);

	if(*backgrounded){
		return 0;
	}
	return EXIT_STATUS(outStatus);
}

int openIO(CMD *cmd, int *to, int *from, int oldTo, int oldFrom){
	if(cmd->toType == RED_OUT){//redirect output
		(*to) = open(cmd->toFile,
					O_WRONLY | O_TRUNC | O_CREAT,
					S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
		if((*to) < 0){
			perror("open");
			return errno;
		}
	} else if(cmd->toType == RED_OUT_APP){//redirect output append
		(*to) = open(cmd->toFile,
					O_APPEND | O_CREAT | O_RDWR,
					S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
		if((*to) < 0){
			perror("open");
			return errno;
		}
	} else{
		//output is just default output
		(*to) = oldTo;
	}
	if(cmd->fromType == RED_IN){//redirect input
		if((*from) != STDI){
			close((*from));
		}
		(*from) = open(cmd->fromFile,O_RDONLY);
		if((*from) < 0){
			perror("open");
			return errno;
		}
	} else{
		//input is just default input
		(*from) = oldFrom;
	}
	return 0;
}

void closeIO(int to, int from,int oldTo, int oldFrom){
	if((to != STDO) && (oldTo == STDO)){//close output stream
		close(to);
	}
	if((from != STDI) && (oldFrom == STDI)){//close input stream
		close(from);
	}
}

void setLocal(CMD *cmd, int to, int from, int oldTo, int oldFrom,
				char *names[], char *values[]){
	int curr = 0;//index in names and values to store into
	for(int i=0;i<cmd->nLocal;i++){
		if(getenv(cmd->locVar[i]) != 0){//store old definitions
			names[curr] = cmd->locVar[i];
			values[curr] = getenv(cmd->locVar[i]);
			curr++;
		}
		if(setenv(cmd->locVar[i],cmd->locVal[i],1) != 0){//set new definitions
			perror("setenv");
			closeIO(to,from,oldTo,oldFrom);
			exit(errno);
		}
	}
	names[curr] = 0;
	values[curr] = 0;
}

void unsetLocal(CMD *cmd, int to, int from, int oldTo, int oldFrom,
				char *names[], char *values[]){
	for(int i=0;i<cmd->nLocal;i++){
		if(unsetenv(cmd->locVar[i]) != 0){//unset all variables
			perror("setenv");
			closeIO(to,from,oldTo,oldFrom);
			exit(errno);
		}
	}
	int j=0;
	while(names[j] != 0){
		if(setenv(names[j],values[j],1) != 0){//put back original definitions
			perror("setenv");
			closeIO(to,from,oldTo,oldFrom);
			exit(errno);
		}
		j++;
	}
}

int processStage(CMD *cmd, int *backgrounded,int oldTo, int oldFrom){
	int rightNoBack = (*backgrounded);//original backgrounded status
		//used for checking if the right child should be backgrounded
	int to = STDO;//output, default to stdout
	int from = STDI;//input, default to stdin
	if(openIO(cmd,&to,&from,oldTo,oldFrom) != 0){//open output and input
		closeIO(to,from,oldTo,oldFrom);
		return errno;
	}
	int leftStatus = 0;//status of left child
	pid_t pid;//pid of child
	int status = 0;//status of child
	int secondFlag = 1;//whether to perform right process
	if((cmd->left != 0) && (cmd->type != SUBCMD)){
		//if not a subcommand, just perform left
		leftStatus = processHelper(cmd->left,backgrounded,oldTo,oldFrom);
	}
	char *envNames[cmd->nLocal];//store old environment variable names
	char *envVals[cmd->nLocal];//store old environment variable values
	int reapedPid;//pid of reaped child
	switch(cmd->type){
		case SIMPLE:
			if(strcmp(cmd->argv[0],"cd") == 0){
				//cd not handled by execvp
				setLocal(cmd,to,from,oldTo,oldFrom,envNames,envVals);
				if(cmd->argc < 2){
					//if no second argument change directory to home
					if(chdir(getenv("HOME")) == SYS_ERR_FOUND){
						perror("chdir");
						closeIO(to,from,oldTo,oldFrom);
						return errno;
					}
				} else{
					//change to specified directory
					if(chdir(cmd->argv[1]) == SYS_ERR_FOUND){
						perror("chdir");
						closeIO(to,from,oldTo,oldFrom);
						return errno;
					}
				}
				unsetLocal(cmd,to,from,oldTo,oldFrom,envNames,envVals);
			} else if(strcmp(cmd->argv[0],"dirs") == 0){
				//dirs not handled by execvp
				setLocal(cmd,to,from,oldTo,oldFrom,envNames,envVals);
				char *buffer = malloc(sizeof(char)*(PATH_MAX + 1));
					//buffer for directory path
				if(getcwd(buffer,PATH_MAX + 1) == 0){
					free(buffer);
					perror("getcwd");
					closeIO(to,from,oldTo,oldFrom);
					return errno;
				}
				unsetLocal(cmd,to,from,oldTo,oldFrom,envNames,envVals);
				//print directory path
				printf("%s\n",getcwd(buffer,PATH_MAX + 1));
				free(buffer);
			} else if(strcmp(cmd->argv[0],"wait") == 0){
				//wait not handled by execvp
				//wait until all children are reaped
				while((reapedPid = waitpid(((pid_t) (-1)), &status, 0)) != -1){
					if(reapedPid != 0){
						fprintf(stderr,"Completed: %d (%d)\n",
								reapedPid,
								status);
					}
				}
			} else {
				//simple command handleable by execvp
				pid = fork();//fork off child process
				if(pid == 0){
					//child process
					signal(SIGINT, sigint_handler_child);
					if(dup2(to,STDO) == SYS_ERR_FOUND){//redirect output
						perror("dup2");
						closeIO(to,from,oldTo,oldFrom);
						exit(errno);
					}
					if(dup2(from,STDI) == SYS_ERR_FOUND){//redirect input
						perror("dup2");
						closeIO(to,from,oldTo,oldFrom);
						exit(errno);
					}
					setLocal(cmd,to,from,oldTo,oldFrom,envNames,envVals);
					status = execvp(cmd->argv[0],cmd->argv);//execute process
					if(status < 0){//error found in execvp
						perror("execvp");
						closeIO(to,from,oldTo,oldFrom);
						exit(errno);
					}
					unsetLocal(cmd,to,from,oldTo,oldFrom,envNames,envVals);
					exit(status);
				} else if(pid < 0){
					//error
					perror("fork");
					closeIO(to,from,oldTo,oldFrom);
					return errno;
				} else{
					//parent waits for chlid to finish
					waitpid(pid, &status, 0);
				}
			}
			break;
		case SUBCMD:
			//subcommand, so go into processSub
			status = processSub(cmd,backgrounded,oldTo,oldFrom);
			secondFlag = 0;
			break;
		default:
			fprintf(stderr,"Entered wrong function!\n");
			break;
	}
	int rightStatus = 0;//status of right child
	if((cmd->right != 0) && secondFlag){//run right child
		rightStatus = processHelper(cmd->right,&rightNoBack,oldTo,oldFrom);
	}
	if(leftStatus != 0){//status checks if overall there was an error
		status = leftStatus;
	}
	if(rightStatus != 0){//status checks if overall there was an error
		status = rightStatus;
	}
	closeIO(to,from,oldTo,oldFrom);//close input and output streams
	if(*backgrounded){
		//backgrounded commands return status 0
		return 0;
	}
	return EXIT_STATUS(status);
}

int processSub(CMD *cmd, int *backgrounded, int oldTo, int oldFrom){
	pid_t pid;//process id of child
	int status = 0;//status of child
	int to = STDO;//output stream, initialize to stdout
	int from = STDI;//input stream, initialize to stdin
	if(openIO(cmd,&to,&from,oldTo,oldFrom) != 0){//set to and from
		closeIO(to,from,oldTo,oldFrom);
		return errno;
	}
	if(cmd->type != SUBCMD){
		fprintf(stderr,"In wrong function!\n");
		return MY_ERR;
	}
	//fork off child process
	pid = fork();
	if(pid == 0){
		//child process
		signal(SIGINT, sigint_handler_child);
		if(dup2(to,STDO) == SYS_ERR_FOUND){//redirect input
			perror("dup2");
			closeIO(to,from,oldTo,oldFrom);
			exit(errno);
		}
		if(dup2(from,STDI) == SYS_ERR_FOUND){//redirect output
			perror("dup2");
			closeIO(to,from,oldTo,oldFrom);
			exit(errno);
		}
		//process left child and exit with the status of it
		status = processHelper(cmd->left,backgrounded,to,from);
		exit(status);
	} else if(pid < 0){
		//error
		perror("fork");
		closeIO(to,from,oldTo,oldFrom);
		return errno;
	} else{
		//parent process
		//waits for child to finish
		waitpid(pid, &status, 0);
	}
	//close to and from
	closeIO(to,from,STDO,STDI);
	if(*backgrounded){
		//backgrounded commands exit with status 0
		return 0;
	}
	return status;
}

int processHelper(CMD *cmd, int *backgrounded,int oldTo, int oldFrom){
	if(cmd->type == NONE){
		return 1;
	}
	int status = 0;//status of child
	int reapedPid;//process id of reaped child

	while(((reapedPid = waitpid((pid_t)-1, &status, WNOHANG)) != -1)
			&& (reapedPid != 0)){//reap children
		fprintf(stderr,"Completed: %d (%d)\n",reapedPid,status);
	}
	int rightNoBack = (*backgrounded);//original backgrounded status
		//used to check if right child should be backgrounded
	if(cmd == 0){//if at null children of leaves
		return 1;
	}
	status = 0;//reinitialize status to zero
	int statusChanged = 0;//whether the status changed in process
	int endLeft = 0;//zero to pass into left if not backgrounded
	int leftStatus = 0;//status of left child
	int rightStatus = 0;//status of right child
	int secondFlag = 1;//whether to run second child
	pid_t pid;//process id of child
	char toSet[VARSIZE];//stores value to be set into ?
	if((cmd->type == SEP_BG) && (cmd->left != 0)){//background
		(*backgrounded) = 1;
		if(cmd->left->type != SEP_END){
			//background entire left subtree
			//fork off child process
			pid = fork();
			if(pid == 0){
				//child process
				signal(SIGINT, sigint_handler_child);
				//process left child
				leftStatus = processHelper(cmd->left,
											backgrounded,
											oldTo,
											oldFrom);
				exit(leftStatus);
			} else if(pid < 0){
				perror("fork");
				return errno;
			} else {
				sprintf(toSet,"%d",0);
				setenv("?",toSet,1);
				fprintf(stderr,"Backgrounded: %d\n",pid);
			}
		} else{
			//dont background subtree left of ;
			//just process it directly
			leftStatus = processHelper(cmd->left->left,
										&rightNoBack,
										oldTo,
										oldFrom);
			//fork off child process
			pid = fork();
			if(pid == 0){
				//child process
				signal(SIGINT, sigint_handler_child);
				//run child process
				leftStatus = processHelper(cmd->left->right,
											backgrounded,
											oldTo,
											oldFrom);
				exit(leftStatus);
			} else if(pid < 0){
				perror("fork");
				return errno;
			} else {
				//don't wait for child process
				//but set ? as needed
				sprintf(toSet,"%d",0);
				setenv("?",toSet,1);
				fprintf(stderr,"Backgrounded: %d\n",pid);
			}
		}
	}
	if(cmd->type != PIPE){
		if((cmd->left != 0) && (cmd->type != SUBCMD)){
			//if not pipe or subcommand run left as below
			if((cmd->type != SEP_END) && (cmd->type != SEP_BG)){
				//run left with backgrounded flag
				leftStatus = processHelper(cmd->left,
											backgrounded,
											oldTo,
											oldFrom);
			} else if(cmd->type != SEP_BG){
				//run left, definitely not backgrounded
				leftStatus = processHelper(cmd->left,
											&endLeft,
											oldTo,
											oldFrom);
			}
		}

		switch(cmd->type){
			case SIMPLE:
				//process simple as stage
				status = processStage(cmd,backgrounded,oldTo,oldFrom);
				if(!(*backgrounded)){//used for whether to change ?
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
				//process simple as stage
				status = processStage(cmd,backgrounded,oldTo,oldFrom);
				if(!(*backgrounded)){//used for whether to change ?
					statusChanged = 1;
				}
				break;
			case NONE:
				break;
			default:
				break;
		}
		if((cmd->right != 0) && secondFlag){
			//if right needs to be processed, process it
			rightStatus = processHelper(cmd->right,&rightNoBack,oldTo,oldFrom);
		}
	} else{
		//process pipe
		status = processPipe(cmd,backgrounded,oldTo,oldFrom);
		if(!(*backgrounded)){//used for whether to change ?
			statusChanged = 1;
		}
	}
	if(statusChanged){//if we ran a simple, a sub or a pipe, set ?
		sprintf(toSet,"%d",status);
		setenv("?",toSet,1);
	}
	if(leftStatus != 0){//final status is that of left right and cmd
		status = leftStatus;
	}
	if(rightStatus != 0){//final status is that of left right and cmd
		status = rightStatus;
	}
	if(*backgrounded){//status of backgrounded is 0
		return 0;
	}
	return status;
}

int process(CMD *cmd){
	signal(SIGINT, sigint_handler_general);
	int backgrounded = 0;
	return processHelper(cmd,&backgrounded,STDO,STDI);
}