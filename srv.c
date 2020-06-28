//---------------------------include start
#include <stdio.h>
#include <strings.h>
#include <string.h>

#include <netinet/in.h>//sockaddr_in

#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <wait.h>

#include <unistd.h>//close()

#include <signal.h>//sigaction {str sig}

#include <errno.h>//errno
//#include <bits/sigaction.h>

//---------------------------include end

//---------------------------define start
//#define CATFEE_DEBUG
#define MAX_CMD_STR 100
#define bprintf(fp, format, ...) \
	if(fp == NULL){printf(format, ##__VA_ARGS__);} 	\
	else{printf(format, ##__VA_ARGS__);	\
			fprintf(fp, format, ##__VA_ARGS__);}
typedef struct _pduStruct{
    int pPin;
    int pLen;
    char pData[MAX_CMD_STR+1];
}pduStruct;
//---------------------------define end

//---------------------------func declaration start
int echo_rep(int connfd, FILE *resFp);
void sig_int(int signo);
void sig_pipe(int signo);
void sig_chld(int signo);
//---------------------------func declaration end

//---------------------------global var dec start
int pipeRcvd = 0;
int sig_to_exit = 0;
//---------------------------global var dec end

int main(int argc, char *argv[]){

    if(argc != 3){
        printf("srv:usage: ./srv <ip> <port>\n");
        return -1;
    }

    //register the signal function
    struct sigaction sigact_pipe, old_sigact_pipe;
    sigact_pipe.sa_handler = sig_pipe;
    sigemptyset(&sigact_pipe.sa_mask);
    sigact_pipe.sa_flags = 0;
    sigact_pipe.sa_flags |= SA_RESTART;
    sigaction(SIGPIPE, &sigact_pipe, &old_sigact_pipe);

    struct sigaction sigact_int;
    sigact_int.sa_handler = sig_int;
    sigemptyset(&sigact_int.sa_mask);
    sigact_int.sa_flags = 0;
    sigaction(SIGINT, &sigact_int, NULL);

    struct sigaction sigact_chld;
    sigact_chld.sa_flags = 0;
    sigact_chld.sa_handler = sig_chld;
    sigemptyset(&sigact_chld.sa_mask);
    sigaction(SIGCHLD, &sigact_chld, 0);

    char resName[20] = {0};
    sprintf(resName, "stu_srv_res_p.txt");
    FILE *resFp = fopen(resName, "w");
    if(!resFp){
        printf("[error] server (%d) fail to open resPfile\n", getpid());
        return -1;
    }
    printf("[srv](%d) %s is opened!\n", getpid(), resName);
    
    bprintf(resFp, "[srv](%d) server[%s:%s] is initializing!\n", getpid(), argv[1], argv[2]);
    fflush(resFp);

    int listenfd;
    struct sockaddr_in servaddr;
    while((listenfd = socket(AF_INET,SOCK_STREAM,0)) == -1);

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(atoi(argv[2]));
    inet_pton(AF_INET, argv[1], &(servaddr.sin_addr));

    if(bind(listenfd, (struct sockaddr*)&servaddr, sizeof(struct sockaddr)) < 0){
        printf("[error] server (%d) fail to bind\n", getpid());
        return -1;
    }

    listen(listenfd, 5);

    int connfd;
    struct sockaddr_in cli_addr;
    int sin_size = sizeof(struct sockaddr_in);
    char IPdotdec[20] = {0};
    char strPort[8] = {0};
    int pid;
    while(1){
        connfd = accept(listenfd, (struct sockaddr*)&cli_addr, &sin_size);
        if((connfd == -1 && errno == EINTR)||(1 == sig_to_exit)){
            close(listenfd);
            bprintf(resFp, "[srv](%d) listenfd is closed!\n", getpid());
            bprintf(resFp, "[srv](%d) parent process is going to exit!\n", getpid());
            fclose(resFp);
            waitpid(-1, NULL, 0);
            printf("[srv](%d) %s is closed!\n", getpid(), resName);
            return 0;
        }

        pid = fork();
        if(0 != pid){
            close(connfd);
            waitpid(-1, NULL, 0);
        }
        else{
            close(listenfd);
            if(-1 == connfd){
                close(connfd);
                printf("[srv] fail to accept!\n");
            }
            else{
                char resPidName[20];
                sprintf(resPidName, "stu_srv_res_%d.txt", getpid());
                FILE *resPidFp = fopen(resPidName, "w");
                if(!resPidFp){
                    printf("[error] server (%d) fail to open resPidfile\n", getpid());
                    return -1;
                }
                //printf("[srv](%d) %s is opened!\n", getpid(), resPidName);


                bprintf(resPidFp, "[srv](%d) listenfd is closed!\n", getpid());
                inet_ntop(AF_INET, &cli_addr.sin_addr, IPdotdec, 16);
                sprintf(strPort, "%d", ntohs(cli_addr.sin_port));
                bprintf(resFp, "[srv](%d) client[%s:%s] is accepted!\n", getpid(), IPdotdec, strPort);

                
                bprintf(resPidFp, "[cli](%d) child process is created!\n", getpid());
                
                int cliPin = echo_rep(connfd, resPidFp);

                char newName[20];
                sprintf(newName, "stu_srv_res_%d.txt", cliPin);
                rename(resPidName, newName);
                bprintf(resPidFp, "[srv](%d) res file rename done!\n", getpid());

                close(connfd);
                bprintf(resPidFp, "[srv](%d) connfd is closed!\n", getpid());

                bprintf(resPidFp, "[srv](%d) child process is going to exit!\n", getpid());

                fclose(resPidFp);
                printf("[srv](%d) %s is closed!\n", getpid(), resPidName);

                return 0;
            }
        }//else

        if(1 == sig_to_exit && 0 != pid){
                close(listenfd);
                fclose(resFp);
                waitpid(-1, NULL, 0);
                printf("[srv](%d) %s is closed!\n", getpid(), resName);
                return 0;
        }
    }

    return 0;

}

int echo_rep(int connfd, FILE *resFp){
    int cliPin = -1;
    pduStruct pduBuf;
    char realdata[8+MAX_CMD_STR+1];
    int readCnt;
    pipeRcvd = 0;
    
    while(1){
        if(1 == sig_to_exit|| 1 == pipeRcvd){
            return cliPin;
        }

        readCnt = 0;
        readCnt = read(connfd, &realdata, sizeof(realdata));
            
        if(readCnt == 0){
            return cliPin;
        }

        memcpy(&pduBuf.pPin, realdata, sizeof(int));
        memcpy(&pduBuf.pLen, realdata+sizeof(int), sizeof(int));
        memcpy(pduBuf.pData, realdata+2*sizeof(int), MAX_CMD_STR);
        cliPin = ntohl(pduBuf.pPin);
        pduBuf.pPin = ntohl(pduBuf.pPin);
        pduBuf.pLen = ntohl(pduBuf.pLen);
        
        bprintf(resFp, "[echo_rqt](%d) %s\n", getpid(), pduBuf.pData);
        
        write(connfd, &realdata, 2*sizeof(int)+pduBuf.pLen);

        if(1 == pipeRcvd){
            return cliPin;
        }
        memset(&pduBuf, 0, sizeof(pduStruct));
        memset(realdata, 0, sizeof(realdata));
        
    }
}

void sig_int(int signo){
    printf("[srv] SIGINT is coming!\n");
    sig_to_exit = 1;
}

void sig_pipe(int signo){
    printf("[srv](%d) SIGPIPE is coming!\n", getpid());
    pipeRcvd = 1;
}

void sig_chld(int signo){
    pid_t pid_chld;
    while((pid_chld = waitpid(-1, NULL, WNOHANG)) > 0);
    //printf("[srv](%d) server child(%d) terminated.\n", getpid(), pid_chld);
}