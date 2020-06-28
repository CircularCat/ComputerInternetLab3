#include <stdio.h>
#include <strings.h>
#include <string.h>

#include <netinet/in.h>//sockaddr_in

#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <wait.h>

#include <signal.h>

#include <unistd.h>//close()


/*define start*/
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
/*define end*/

int echo_rqt(int sockfd, int pin, FILE *resFp);
void sig_int(int signo);
void sig_chld(int signo);

int sig_to_exit = 0;

int main(int argc, char *argv[]){

    if(argc != 4){
        printf("[cli<main>]: usage: ./cli [ip] [port] [maxProcNum]\n");
        return -1;
    }
    int procNum = argv[3][0]-'0';//the process number

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

    printf("%d start\n", getpid());//father
    int i = 0, pidRet;
    for(i=1; i<procNum; i++){
        pidRet = fork();
        
        if(0 == pidRet){    
            printf("[cli] client%d(%d) start\n", i, getpid());
            break;
        }
        if(-1 == pidRet){
            printf("--proc create fail--\n");
            return -1;
        }
    }

    int myPin = i;
    char resName[] = "stu_cli_res_x.txt";
    if(0 == pidRet){
        resName[12] = myPin+'0';
    }
    else
    {
        myPin = 0;
        resName[12] = 0+'0';
    }
    FILE *resFp = fopen(resName, "w");
    if(!resFp){
        printf("[error] client %d(%d) fail to open resfile\n", myPin, getpid());
        return -1;
    }
    printf("[cli](%d) child process %d is created!\n", getpid(), myPin);

    //conn to server
    int sockfd;
    struct sockaddr_in servaddr;
    while((sockfd = socket(AF_INET,SOCK_STREAM,0)) == -1);
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(atoi(argv[2]));
    inet_pton(AF_INET, argv[1], &(servaddr.sin_addr));
    while(0 != connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)));
    bprintf(resFp, "[cli](%d) server[%s:%s] is connected!\n", getpid(), argv[1], argv[2]);

    echo_rqt(sockfd, myPin, resFp);

    //close(sockfd);
    bprintf(resFp, "[cli](%d) connfd is closed!\n", getpid());

    if(0 == pidRet){
        bprintf(resFp, "[cli](%d) child process is going to exit!\n", getpid());
    }
    else{
        bprintf(resFp, "[cli](%d) parent process is going to exit!\n", getpid());
    }
    

    fclose(resFp);
    printf("[cli](%d) %s is closed!\n", getpid(), resName);

    return 0;
}

int echo_rqt(int sockfd, int pin, FILE *resFp){
    char tdName[10] = "tdx.txt";
    tdName[2] = pin+'0';
    FILE *tdFp = fopen(tdName, "r");
    if(!tdFp){
        printf("[error] client %d(%d) fail to open tdfile\n", pin, getpid());
        return -1;
    }
    printf("[cli] %d(%d) succ to open tdfile\n", pin, getpid());
    

    char *p;
    char temp;
    pduStruct pduBuf;
    int hLen;
    char realdata[8+MAX_CMD_STR+1];
    memset(&pduBuf, 0, sizeof(pduStruct));
    while(sig_to_exit == 0){
        p = pduBuf.pData;
        *p = fgetc(tdFp);

        while(*p!='\n'&&p<pduBuf.pData+MAX_CMD_STR-1&&*p!=EOF){
            *(++p) = fgetc(tdFp);

        }
        
        if(strncmp(pduBuf.pData, "exit", 4) == 0){
            close(sockfd);
            return 0;
        }

        temp = *p;
        *p = '\0';

        if(EOF == temp){
            //return 0;
        }
        if(1 == sig_to_exit){
            return 0;
        }

        hLen = strnlen(pduBuf.pData, MAX_CMD_STR)+1;
        pduBuf.pPin = htonl(pin);
        pduBuf.pLen = htonl(hLen);

        memcpy(realdata, &pduBuf.pPin, sizeof(int));
        memcpy(realdata+sizeof(int), &pduBuf.pLen, sizeof(int));
        memcpy(realdata+2*sizeof(int), pduBuf.pData, hLen);
        write(sockfd, &realdata, 2*sizeof(int)+hLen);

        //write(sockfd, &pduBuf, sizeof(pduBuf));
        pduBuf.pLen = 0;
        memset(pduBuf.pData, 0, sizeof(pduBuf.pData));
        memset(realdata, 0, sizeof(realdata));

        read(sockfd, &realdata, sizeof(realdata));

        memcpy(&pduBuf.pPin, realdata, sizeof(int));
        memcpy(&pduBuf.pLen, realdata+sizeof(int), sizeof(int));
        memcpy(pduBuf.pData, realdata+2*sizeof(int), MAX_CMD_STR);
        pduBuf.pPin = ntohl(pduBuf.pPin);
        pduBuf.pLen = ntohl(pduBuf.pLen);
        bprintf(resFp, "[echo_rep](%d) %s\n", getpid(), pduBuf.pData);
        memset(pduBuf.pData, 0, sizeof(pduBuf.pData));
        pduBuf.pLen = 0;
        if(1 == sig_to_exit){
            return 0;
        }
    }//while
}

void sig_int(int signo){
    printf("[srv] SIGINT is coming!\n");
    sig_to_exit = 1;
}

void sig_chld(int signo){
    pid_t pid_chld;
    while((pid_chld = waitpid(-1, NULL, WNOHANG)) > 0);
    //printf("[cli](%d) server child(%d) terminated.\n", getpid(), pid_chld);
}