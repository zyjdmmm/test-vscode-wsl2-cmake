#include<stdio.h>//perror 
#include <stdlib.h>//exit

#include<sys/socket.h>//socket

#include<string.h>//bzero
#include<netinet/in.h>//struct sockaddr_in 
#include<unistd.h>//close 
#include <ctype.h>//isspace 
#include<sys/stat.h>//struct stat,
#include<sys/wait.h>//waitpid
#include <netinet/in.h>//struct socketaddr
#include <arpa/inet.h>//inet_ntoa

#include"threadpool.h"

#define ISspace(x) isspace((int)(x)) //检查是否为空格、tab、回车，是则返回非0值（参数可能为char，需要强制转换为int）
#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"//似乎没什么意义的一句话？
//经典报错函数
void perr_exit(const char *s){
	perror(s);
	exit(-1);
}


//初始化服务器三步走,返回监听套接字listen_fd的文件描述符
int startup(unsigned short *port){
	int listen_fd;//门卫套接字的文件描述符
    struct sockaddr_in serv_addr;

    //s 
    listen_fd=socket(PF_INET,SOCK_STREAM,0);
    if(listen_fd==-1){//黑马的教程是<0
        perr_exit("socket");
    }

    //端口复用：总感觉有问题？？？
    // int opt = 1;
    // if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const void *) &opt, sizeof(opt))) {
    //     perr_exit("setsockopt");
    // }

    //b
    bzero(&serv_addr,sizeof(serv_addr));
    serv_addr.sin_family=PF_INET;
    serv_addr.sin_port=htons(*port);//port
    serv_addr.sin_addr.s_addr=htonl(INADDR_ANY);//ip
    if(bind(listen_fd,(struct sockaddr *)&serv_addr,sizeof(serv_addr))<0){
        perr_exit("bind");        
    }
    //未使用：inet_ntoa(serv_addr.sin_addr) inet_ntoa将以网络字节序给出的网络主机地址 in 转换成以点分十进制表示的字符串（如127.0.0.1），注意这个函数不用再输入第三级的地址族了
    printf("当前的端口号是 1.15.144.179:%d\n",ntohs(serv_addr.sin_port));//我把ip写成公网地址好复制

    //我认为是不需要这一段的，不过getsockname似乎有其用处？
    // //如果调用 bind 后端口号仍然是0，则手动调用getsockname()获取端口号
    // if (*port == 0)  /* if dynamically allocating a port */
    // {
    // int namelen = sizeof(name);
    // //调用getsockname()获取系统给 httpd 这个 socket 随机分配的端口号
    // if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
    // error_die("getsockname");
    // *port = ntohs(name.sin_port);
    // }

    //l
    if(listen(listen_fd,128)<0){
        perr_exit("listen");
    }    

    return listen_fd;
}


//获取http协议一行的数据经典程序，注意http协议每一行以\r\n结尾，但是这个函数会统一为'\n'结尾（黑马也是用的这一套代码）
int get_line(int sock, char *buf, int size){
    int i = 0;
    char c = '\0';
    int n;

    //每次只读一个字符
    while ((i < size - 1) && (c != '\n')){//遇到'\n'直接退出
        //读一个字节的数据存放在 c 中
        n = recv(sock, &c, 1, 0);//读取一个字符
        /* DEBUG printf("%02X\n", c); */
        if (n > 0){
            if (c == '\r'){//若是'\r'就继续读，因为可能是'\r''\n'换行的
                n = recv(sock, &c, 1, MSG_PEEK);//MSG_PEEK:偷看一个字符（peek: vi.偷看）
                /* DEBUG printf("%02X\n", c); */
                if ((n > 0) && (c == '\n')){//看看预读的数是否是'\n
                    recv(sock, &c, 1, 0);//特别注意：他这里'\r'被替换为'\n'，也就是说如果这一行是'\r\n'，最后这行只会读取到'\n'!!!
                }else{
                    c = '\n';//如果下个数不是'\n'，就把'\r'也替换成'\n'
                }
            }
            buf[i] = c;
            i++;
        }else{
            c = '\n';
        }
    }

    buf[i] = '\0';//最后以'\0'结尾
    return(i);
}

//返回给客户端html的header
void headers(int connect_fd, const char *filename){
    char buf[1024];
    (void)filename;  /* could use filename to determine file type */

    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(connect_fd, buf, strlen(buf), 0);
    strcpy(buf, SERVER_STRING);
    send(connect_fd, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");//奇怪的是他这里不用上两句的strcpy()？？？
    send(connect_fd, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(connect_fd, buf, strlen(buf), 0);
}

//向客户端发送文件本体
void cat(int connect_fd, FILE *resource){
    char buf[1024];

    //经典程序：按行读取文件并按行发送
    fgets(buf, sizeof(buf), resource);//我感觉这里不需要这句，并把下面while两句调换位置？？？
    while (!feof(resource)){
        send(connect_fd, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource);//按行读取到buf中
    }
}

//404错误页面
void not_found(int connect_fd){
 char buf[1024];

    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(connect_fd, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(connect_fd, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(connect_fd, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(connect_fd, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(connect_fd, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(connect_fd, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(connect_fd, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(connect_fd, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(connect_fd, buf, strlen(buf), 0);
}

//向客户端发送header后发送文件
void serve_file(int connect_fd, const char *filename){
    FILE *resource = NULL;
    int numchars = 1;
    char buf[1024];

    //确保 buf 里面有东西，能进入下面的 while 循环
    buf[0] = 'A'; buf[1] = '\0';
    //循环作用是读取并忽略掉这个 http 请求后面的header？？？那剩下的内容呢？？？
    while ((numchars > 0) && strcmp("\n", buf)){//strcmp()字符串相同则返回0，这里比较结果不同继续循环
        numchars = get_line(connect_fd, buf, sizeof(buf));
    }

    //打开这个传进来的这个路径所指的文件
    resource = fopen(filename, "r");
    if (resource == NULL){
        not_found(connect_fd);
    }else{
        //打开成功后，将这个文件的基本信息封装成 response 的头部(header)并返回
        headers(connect_fd, filename);
        //接着把这个文件的内容读出来作为 response 的 body 发送到客户端
        cat(connect_fd, resource);
    }

    fclose(resource);
}

//500报错：无法执行cgi程序
void cannot_execute(int client){
 char buf[1024];

 sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "Content-type: text/html\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
 send(client, buf, strlen(buf), 0);
}

//400报错：未找到body大小参数
void bad_request(int client){
 char buf[1024];

 sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
 send(client, buf, sizeof(buf), 0);
 sprintf(buf, "Content-type: text/html\r\n");
 send(client, buf, sizeof(buf), 0);
 sprintf(buf, "\r\n");
 send(client, buf, sizeof(buf), 0);
 sprintf(buf, "<P>Your browser sent a bad request, ");
 send(client, buf, sizeof(buf), 0);
 sprintf(buf, "such as a POST without a Content-Length.\r\n");
 send(client, buf, sizeof(buf), 0);
}

//由于客户端请求GET POST之外的方法，返回给客户端501错误页面
void unimplemented(int connect_fd){
    char buf[1024];

    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(connect_fd, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(connect_fd, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(connect_fd, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(connect_fd, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(connect_fd, buf, strlen(buf), 0);
    sprintf(buf, "</TITLE></HEAD>\r\n");
    send(connect_fd, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
    send(connect_fd, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(connect_fd, buf, strlen(buf), 0);
}

//利用父进程充当中介进行读写，而子进程执行cgi程序
//这里要创建两个进程的原因是子进程被替换为cgi脚本后无法和socket通信了，所以需要父进程充当中介负责读写socket（来自chatgpt）
void execute_cgi(int connect_fd, const char *path,
                 const char *method, const char *query_string){
    char buf[1024];
    int cgi_output[2];
    int cgi_input[2];
    pid_t pid;
    int status;
    int i;
    char c;
    int numchars = 1;
    int content_length = -1;

    /*********************处理GET和POST：GET抛弃header所有内容，POST读取header中body长度，两者都保留body**************************************/
    //往 buf 中填东西以保证能进入下面的 while
    buf[0] = 'A'; buf[1] = '\0';//我感觉这里写get_line()来做填充也是可以的？？？

    /***********************GET方法：抛弃header但保留body*****************/
    //这里url的长度似乎受到buf[1024]的影响，只接受1024bit？？？
    if (strcasecmp(method, "GET") == 0){
        while ((numchars > 0) && strcmp("\n", buf)){
            numchars = get_line(connect_fd, buf, sizeof(buf));
        }

    /***********************POST方法：只读取header中body的长度，但保留body************/
    }else{
        //这一句并不是忽略第一行的url，而是填入东西以保证能进入下面的循环
        numchars = get_line(connect_fd, buf, sizeof(buf));

        //读取header中的记录的body长度记录，并忽略header其他所有内容
        //注意这里只读完 header 的内容，body 的内容没有读,为什么？？因为header和body之间的分隔是一个空行，由于系统差别可能是'\r\n'或'\n',但由于上面get_line()函数的原因会统一为'\n'结尾
        while ((numchars > 0) && strcmp("\n", buf)){
            buf[15] = '\0';//'Content-Length:'的长度是15,补上'\0'后，后面的内容是body的大小
            if (strcasecmp(buf, "Content-Length:") == 0){//Content-Length记录了body的大小
                content_length = atoi(&(buf[16]));
            }
            numchars = get_line(connect_fd, buf, sizeof(buf));
        }

        //检查header中是否有指示 body 长度大小的参数
        if (content_length == -1) { 
            bad_request(connect_fd);
            return;
        }
    }

    sprintf(buf, "HTTP/1.0 200 OK\r\n");//正确接收POST或者GET请求并向浏览器报告正确
    send(connect_fd, buf, strlen(buf), 0);

    /*****************************************创建父子进程****************************************************/
    //下面这里创建两个管道，用于两个进程间通信
    //我认为还可以只定义一个管道，在fork()之后就复制了一份？？？
    if (pipe(cgi_output) < 0) {
        cannot_execute(connect_fd);
        return;
    }
    if (pipe(cgi_input) < 0) {
        cannot_execute(connect_fd);
        return;
    }

    //创建一个子进程
    if ( (pid = fork()) < 0 ) {
        cannot_execute(connect_fd);
        return;
    }

    //子进程用来执行 cgi 脚本
    if (pid == 0){
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        //建立两条条管道进行双向通信
        //将子进程的输出由 标准输出 重定向到 cgi_ouput 的管道写端上
        dup2(cgi_output[1], STDOUT_FILENO);
        //将子进程的输出由 标准输入 重定向到 cgi_iuput 的管道读端上
        dup2(cgi_input[0], STDIN_FILENO);
        //关闭 cgi_ouput 管道的读端与cgi_input 管道的写端
        close(cgi_output[0]);//0是读端
        close(cgi_input[1]);//1是写端

        //请求方法REQUEST_METHOD=?声明为环境变量    //这里环境变量究竟有什么用？？？
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        //将这个环境变量加进子进程的运行环境中
        putenv(meth_env);
        // 设置环境变量 char * env_var = "VAR=VALUE"; putenv(env_var);
        // 获取环境变量 char * env_var = getenv("VAR");

        //get的url？后面为参数信息，post的body为参数信息
        if (strcasecmp(method, "GET") == 0) {//get声明url中？后部分为环境变量
            sprintf(query_env, "QUERY_STRING=%s", query_string);//query_string指向url中？后半部分
            putenv(query_env);
        }else {  //post声明body长度为环境变量
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }

        //最后将子进程替换为cgi脚本并执行
        execl(path, path, NULL);
        exit(0);

    //父进程
    } else {    
        //完成进程通信双向管道建立
        close(cgi_output[1]);//父进程则关闭了 cgi_output管道的写端和 cgi_input 管道的读端
        close(cgi_input[0]);

        //如果是 POST 方法,父进程充当中介读取body的内容，并发送给子进程
        if (strcasecmp(method, "POST") == 0){
            for (i = 0; i < content_length; i++){
                recv(connect_fd, &c, 1, 0);
                write(cgi_input[1], &c, 1);
            }
        }

        //GET方法则似乎啥也不干，是的，但是GET的body还残留在socket怎么办？？？

        //父进程充当中介，读子进程的输出并发送到客户端
        while (read(cgi_output[0], &c, 1) > 0){//居然如此简单
            send(connect_fd, &c, 1, 0);
        }

        //关闭管道
        close(cgi_output[0]);//如果进程结束不关闭管道会发发生什么？？？
        close(cgi_input[1]);
        //等待子进程的退出，结束本次请求处理
        waitpid(pid, &status, 0);//阻塞回收
    }
}

//处理来自客户端的需求
void deal_request(void* arg){
    int numchars;
    char buf[1024];
    char method[255];
    char url[255];
    char path[512];
    size_t i=0,j=0;//无符号整数 //保证通用性？？
    char *query_string = NULL;
    int cgi = 0;
    struct stat st;
    int connect_fd;

    connect_fd=*((int *)arg);//先强制转换为int *，在解引用（其他方式似乎会报错？？？）
/*******************************************处理http请求的第一行数据*****************************************************************/
    //从socket中读取http请求的第一行数据
    numchars=get_line(connect_fd,buf,sizeof(buf));
    //找到第一行中的请求方法并存进 method 中
    while (!ISspace(buf[j]) && (i < sizeof(method) - 1)){
        method[i] = buf[j];
        i++; j++;
    }
    method[i] = '\0';
    
    //如果请求的方法不是 GET 或 POST 任意一个的话就直接发送 response 告诉客户端没实现该方法
    if (strcasecmp(method, "GET") && strcasecmp(method, "POST")){
        unimplemented(connect_fd);
        return;
    }

    //如果是 POST 方法就将 cgi 标志变量置一(true)  //cgi是什么蛇皮
    if (strcasecmp(method, "POST") == 0){//相等返回0
        cgi = 1;
    }

    i = 0;
    //跳过GET或POST后跳过所有的空白字符(空格)
    while (ISspace(buf[j]) && (j < sizeof(buf))){
        j++;
    } 

    //然后把 URL 读出来放到 url 数组中
    while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf))){
        url[i] = buf[j];
        i++; j++;
    }
    url[i] = '\0';

    //如果这个请求是一个 GET 方法的话
    if (strcasecmp(method, "GET") == 0){//比较字符串，忽略大小
        //用一个指针指向 url
        query_string = url;

        //去遍历这个 url，跳过字符 ？前面的所有字符，如果遍历完毕也没找到字符 ？则退出循环
        while ((*query_string != '?') && (*query_string != '\0'))//发送的请求中?后面携带的是参数
        query_string++;
        //如果有？最后query_string会恰好指向？

        //退出循环后检查当前的字符是 ？还是字符串(url)的结尾
        if (*query_string == '?'){
            //如果是 ？ 的话，证明这个请求需要调用cgi（我理解为刷新页面？），将 cgi 标志变量置一(true)
            cgi = 1;
            //从字符 ？ 处把字符串 url 给分隔会两份
            *query_string = '\0';
            //使指针指向字符 ？后面的那个字符
            query_string++;//query_string指向原url后半部分，新url指向原url前半部分
        }
    }  

    //htdocs拼接url（且不带GET方法'?'后面的字符串）到path
    sprintf(path, "htdocs%s", url);//这两者之间是不是缺个'/'？不是的，我看GET或POST的url总是"/xxx"的形式

    //如果path 数组中的这个字符串的最后一个字符是以字符 '/' 结尾的话，就拼接一个"index.html"字符串。首页的意思
    if (path[strlen(path) - 1] == '/'){
        strcat(path, "index.html");
    }
/********************解析请求的是什么文件？目录文件（替换为index.html网页索引）、可执行文件********************************/
    //似乎没有处理普通文件？？？

    //查询path所指向该文件是否存在
    //如果不存在，把 http 的请求后续的内容(head 和 body)全部读完并忽略
    if (stat(path, &st) == -1) {
        while ((numchars > 0) && strcmp("\n", buf)){//strcmp相等返回0，和'\n'比较的意义是什么？为了找到空行
            numchars = get_line(connect_fd, buf, sizeof(buf));
        }
        //然后返回一个找不到文件的 response 给客户端
        not_found(connect_fd);

    //文件存在，那去跟常量S_IFMT相与，相与之后的值可以用来判断该文件是什么类型的
    }else{
        //S_IFMT参读《TLPI》P281，与下面的三个常量一样是包含在<sys/stat.h>
        //如果path指向文件是个目录，在 path 后面拼接"/index.html"(等于说每个目录下还要放个html文件？)
        if ((st.st_mode & S_IFMT) == S_IFDIR){
            strcat(path, "/index.html");
        }

        //如果这个文件是一个可执行文件，不论是属于用户/组/其他这三者类型的，就将 cgi 标志变量置一
        //S_IXUSR, S_IXGRP, S_IXOTH三者可以参读《TLPI》P295
        //检查文件的访问权限，如果文件有可执行权限,则将cgi设置为1
        if ((st.st_mode & S_IXUSR)|| //（用户-执行）
        (st.st_mode & S_IXGRP)    || //（组-执行）
        (st.st_mode & S_IXOTH)     ){//其他-执行）
            cgi = 1;//为什么前面cgi也多次置1，为什么要多次cgi置1？？？
        }

        if (!cgi){
            serve_file(connect_fd, path);//如果不需要 cgi 机制的话 ，直接传送文件
        }else{
            execute_cgi(connect_fd, path, method, query_string);//如果需要则调用
        }
    }
    close(connect_fd);

    
}

int main(void){
    unsigned short port=9527;
    int listen_fd=-1;//监听套接字（文件描述符）:就是int
    int connect_fd=-1;//建立好通信的套接字（文件描述符）

    struct sockaddr_in clnt_addr;//记录客户端地址结构
	socklen_t clnt_addr_size;//客户端地址结构长度

    listen_fd=startup(&port);
    printf("服务器初始化成功，监听套接字开始监听!\n"); 

    while (1){ 
        //a
        //这里errno=ECONNABORTED或EINTR应该重新调用accept(),这里没有封装accept()是因为while(1)恰好重新调用了
        //这句不检查errno真的不会出问题吗？？？
        connect_fd=accept(listen_fd,(struct sockaddr *)&clnt_addr,&clnt_addr_size);
        if (connect_fd == -1){
            perr_exit("accept");
        }

        //使用线程池
        thread_pool *pool;
        pool=thread_pool_create(3,10,100);
        thread_pool_add(pool,deal_request,&connect_fd);

        //deal_request(connect_fd);//这个地方应该是可以开辟线程处理的？
    }
    
    close(listen_fd);
    return 0;
} 