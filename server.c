#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<sys/wait.h>
#include<sys/types.h>
#include<sys/epoll.h>
#include<unistd.h>
#include<fcntl.h>
#include<sys/stat.h>
#include<errno.h>
#include<dirent.h>
#include<ctype.h>
#define MAXSIZE 2048
// 16进制数转化为10进制
int hexit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;

    return 0;
}
/*
 *  这里的内容是处理%20之类的东西！是"解码"过程。
 *  %20 URL编码中的‘ ’(space)
 *  %21 '!' %22 '"' %23 '#' %24 '$'
 *  %25 '%' %26 '&' %27 ''' %28 '('......
 *  相关知识html中的‘ ’(space)是&nbsp
 */
void encode_str(char* to, int tosize, const char* from)
{
    int tolen;

    for (tolen = 0; *from != '\0' && tolen + 4 < tosize; ++from) {    
        if (isalnum(*from) || strchr("/_.-~", *from) != (char*)0) {      
            *to = *from;
            ++to;
            ++tolen;
        } else {
            sprintf(to, "%%%02x", (int) *from & 0xff);
            to += 3;
            tolen += 3;
        }
    }
    *to = '\0';
}
void decode_str(char *to, char *from)
{
    for ( ; *from != '\0'; ++to, ++from  ) {     
        if (from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2])) {       
            *to = hexit(from[1])*16 + hexit(from[2]);
            from += 2;                      
        } else {
            *to = *from;
        }
    }
    *to = '\0';
}
//通过文件名获取文件类型
const char *get_file_type(const char *name)
{
    char* dot;

    dot = strrchr(name, '.');   
    if (dot == NULL)
        return "text/plain; charset=utf-8";
    if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
        return "text/html; charset=utf-8";
    if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
        return "image/jpeg";
    if (strcmp(dot, ".gif") == 0)
        return "image/gif";
    if (strcmp(dot, ".png") == 0)
        return "image/png";
    if (strcmp(dot, ".css") == 0)
        return "text/css";
    if (strcmp(dot, ".au") == 0)
        return "audio/basic";
    if (strcmp( dot, ".wav" ) == 0)
        return "audio/wav";
    if (strcmp(dot, ".avi") == 0)
        return "video/x-msvideo";
    if (strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0)
        return "video/quicktime";
    if (strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0)
        return "video/mpeg";
    if (strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0)
        return "model/vrml";
    if (strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0)
        return "audio/midi";
    if (strcmp(dot, ".mp3") == 0)
        return "audio/mpeg";
    if (strcmp(dot, ".ogg") == 0)
        return "application/ogg";
    if (strcmp(dot, ".pac") == 0)
        return "application/x-ns-proxy-autoconfig";

    return "text/plain; charset=utf-8";
}
//发送目录内容
void send_dir(int cfd, const char* dirname)
{
	int i, ret;
	//拼一个html页面<table></table>
	char buf[4094] = {0};
	sprintf(buf, "<html><head><title>目录名:%s</title></head>", dirname);
	sprintf(buf + strlen(buf), "<body><h1>当前目录: %s</h1><table>", dirname);
	char enstr[1024] = {0};
	char path[1024] = {0};
	//目录项二级指针
	struct dirent** ptr;
	int num = scandir(dirname, &ptr, NULL, alphasort);
	//遍历
	for(i = 0; i < num; i++) {
		char* name = ptr[i]->d_name;
		//拼接文件的完整路径
		sprintf(path, "%s/%s", dirname, name);
		printf("path = %s ====================\n", path);
		struct stat st;
		stat(path, &st);
		encode_str(enstr, sizeof(enstr), name);
		//如果是文件
		if(S_ISREG(st.st_mode)) {
			sprintf(buf + strlen(buf), 
				"<tr><td><a href=\"%s\">%s</a></td><td></tr>", enstr, name, (long)st.st_mode);
			
		} else if(S_ISDIR(st.st_mode)) {
			sprintf(buf + strlen(buf),
				"<tr><td><a href=\"%s/\">%s/</a></td><td>%ld</td></tr>", 
				enstr, name, (long)st.st_size);
		}
		ret = send(cfd, buf, strlen(buf), 0);
		if (ret == -1) {
			if (errno == EAGAIN) {
				perror("send error");
				continue;
			} else if (errno == EINTR) {
				perror("send error");
				continue;
			} else {
				perror("send error");
				exit(1);
			}
		}
		memset(buf, 0, sizeof(buf));
	}
	sprintf(buf + strlen(buf), "</table></body></html>");
	send(cfd, buf, strlen(buf), 0);
	printf("dir message send OK!!!!\n");
# if 0
	//打开目录
	DIR* dir = opendir(dirname);
	if (dir == NULL) {
		perror("opendir error");
		exit(1);
	}
	//读目录
	struct dirent* ptr = NULL;
	while ((ptr = readdir(dir)) != NULL) {
		char * name = ptr->d_name;
	}
	closedir(dir);
# endif
}
//发送响应头
void send_respond_head(int cfd, int no, const char* desp, const char* type, long len)
{
	char buf[1024] = {0};
	//状态行
	sprintf(buf, "http/1.1 %d %s\r\n", no, desp);
	send(cfd, buf, strlen(buf), 0);
	//消息头
	sprintf(buf, "Content-Type:%s\r\n", type);
	sprintf(buf + strlen(buf), "Content-Length:%ld\r\n", len);
	send(cfd, buf, strlen(buf), 0);
	//空行
	send(cfd, "\r\n", 2, 0);

}
//发送错误页面
void send_error(int cfd, int status, char* title, char* text)
{
	char buf[4096];
	sprintf(buf,"%s %d %s\r\n", "HTTP/1.1",status, title);
	sprintf(buf+strlen(buf),"Content-Type:%s\r\n","text/html");
	sprintf(buf+strlen(buf),"Content-Length:%d\r\n",-1);
	sprintf(buf+strlen(buf),"Connection: close\r\n");
	send(cfd, buf, strlen(buf), 0);
	send(cfd, "\r\n", 2, 0);
	
	memset(buf, 0, sizeof(buf));
	
	sprintf(buf, "<html><head><title>%d %s</title><head>\n", status, title);
	sprintf(buf+strlen(buf), "<body bgcolor=\"#cc99cc\"><h4 align=\"center\">%d %s</h4>\n", status, title);
	sprintf(buf+strlen(buf), "%s\n", text);
	sprintf(buf+strlen(buf), "<hr>\n</body>\n</html>\n");
	send(cfd, buf, strlen(buf), 0);

}
int get_line(int cfd, char* buf, int size)
{
	int i = 0;
	char c = '\0';
	int n;
	while((i < size-1) && (c != '\n')){
		n = recv(cfd, &c, 1, 0);
		if(n > 0){
			if(c == '\r'){
				n = recv(cfd, &c, 1, MSG_PEEK);
				if((n > 0) && (c == '\n')){
					recv(cfd, &c, 1, 0);
				}else{
					c = '\n';
				}
				
			}
			buf[i] = c;
			i++;
		}else{
			c = '\n';
		}
	}
	buf[i] = '\0';
	if(-1 == n){
		i = n;
	}
	return i;
}
//断开连接
void disconnect(int cfd, int epfd)
{
	int ret;
	epoll_ctl(epfd, EPOLL_CTL_DEL, cfd, NULL);
	if(ret == -1) {
		perror("epoll_ctl del cfd error");
		exit(1);
	}
	close(cfd);	
}
//回发应答协议
//参数:连接客户端的套接字文件描述符，http状态码，状态信息，回应文件类型，文件长度
void send_respond(int cfd, int no, char* msg, char* type, int len)
{
	char buf[1024];
	sprintf(buf,"HTTP/1.1 %d %s\r\n",no,msg);
	sprintf(buf+strlen(buf),"%s",type);
	sprintf(buf+strlen(buf),"Content-Length:%d\n",len);
	send(cfd, buf, strlen(buf), 0);
	send(cfd, "\r\n", 2, 0);
}
//回发给客户端请求的数据内容
void send_file(int cfd, const char* file)
{
	int ret;
	int n;
	char buf[4096] = {0};
	//1.获取本地文件的描述符
	int fd = open(file,O_RDONLY);
	if(fd == -1) {
		perror("open error");
		exit(1);
	}
	//2.把文件内容发送给客户端
	while((n = read(fd, buf, sizeof(buf))) > 0) {
		//printf("%d\n",n);
		//printf("buf=%s\n",buf);
		//发送数据给客户端
		ret = send(cfd, buf, n, 0);
		if(ret == -1) {
			if(errno == EAGAIN) {
				printf("-----------EAGAIN\n");
				continue;
			} else if(errno == EINTR) {
				printf("-----------EINTR\n");
				continue;
			} else {
				perror("send error");
				exit(1);
			}
		}
		printf("ret=%d\n", ret);	
	}
	close(fd);
	//exit(0);
}
//处理http请求
void http_request(int cfd, const char* request)
{
	// 拆分http请求行
	char method[12], path[1024], protocol[12];
    	sscanf(request, "%[^ ] %[^ ] %[^ ]", method, path, protocol);
    	printf("method = %s, path = %s, protocol = %s\n", method, path, protocol);
	// 转码 将不能识别的中文乱码 -> 中文
	// 解码 %23 %34 %5f
	decode_str(path, path);                 
	char* file = path+1; // 去掉path中的/ 获取访问文件名

	// 如果没有指定访问的资源, 默认显示资源目录中的内容
	if(strcmp(path, "/") == 0) {    
        // file的值, 资源目录的当前位置
        	file = "./";
        }
	
	// 获取文件属性
	struct stat st;
    	int ret = stat(file, &st);
    	if(ret == -1) { 
        	send_error(cfd, 404, "Not Found", "NO such file or direntry");     
        	return;
    	}
	

	// 判断是目录还是文件
	if(S_ISDIR(st.st_mode)) {  		// 目录 
        // 发送头信息
        	send_respond_head(cfd, 200, "OK", get_file_type(".html"), -1);
        	// 发送目录信息
                send_dir(cfd, file);
        } else if(S_ISREG(st.st_mode)) { // 文件        
                // 发送消息报头
                send_respond_head(cfd, 200, "OK", get_file_type(file), st.st_size);
                // 发送文件内容
                send_file(cfd, file);
        }	
}
void do_read(int cfd,int epfd)
{
	char line[1024];
	//读取一行http协议拆分，获取get 文件名和协议号
	int len = get_line(cfd, line, sizeof(line));
	if(len == 0) {
		perror("get_line error");
		disconnect(cfd, epfd);
		exit(1);
	}else {
		while(1) {//循环目的是去掉http协议的其他部分，以防止堵塞缓冲区
			char buf[1024];
			len = get_line(cfd, buf, sizeof(buf));
			if(len == -1) {
				break;
			}
		}
	 	// 判断get请求
	 	if(strncasecmp("get", line, 3) == 0) { // 请求行: get /hello.c http/1.1   
	        	// 处理http
	 		http_request(cfd, line);                             
	        	//关闭套接字, cfd从epoll上del
	        	disconnect(cfd, epfd);         
	 	}
	}
}
void do_accept(int lfd, int epfd)
{
	struct sockaddr_in client_addr;
	socklen_t client_addr_len = sizeof(client_addr);
	//建立连接
	int cfd = accept(lfd, (struct sockaddr*)&client_addr, &client_addr_len);
	if(cfd == -1){
		perror("accept error");
		exit(1);
	}
	//设置非阻塞
	int flag = fcntl(cfd, F_GETFL);
	flag |= O_NONBLOCK;
	fcntl(cfd, F_SETFL, flag);
	//挂到树上
	struct epoll_event ev;
	ev.events = EPOLLIN | EPOLLET;//边沿非阻塞
	ev.data.fd = cfd;
	int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);
	if(ret == -1){
		perror("epoll_ctl error");
		exit(1);
	}
}
int init_listen_fd(int port, int epfd)
{
	int lfd = socket(AF_INET, SOCK_STREAM, 0);	
	//服务器结构地址
	struct sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	
	//端口复用
	int opt = 1;
	setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, (void*)&opt, sizeof(opt));
	//绑定地址结构
	int ret = bind(lfd,(struct sockaddr*)&server_addr, sizeof(server_addr));
	if(ret == -1){
		perror("bind error");
		exit(1);
	}
	//设置监听上限
	ret = listen(lfd, 128);
	if(ret == -1){
		perror("listen error");
		exit(1);
	}
	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.fd = lfd;
	//挂到树上
	ret = epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);
	if(ret == -1){
		perror("epoll_ctl error");
		exit(1);
	}
	return lfd;
}
//创建树根
//向树添加lfd
void epoll_run(int port)
{
	int i = 0;
	struct epoll_event all_events[MAXSIZE];
	//1.创建树
	int epfd = epoll_create(MAXSIZE);
	if(epfd == -1){
		perror("epoll_create error");
		exit(1);
	}
	//2.创建lfd,并添加到树上
	int lfd = init_listen_fd(port, epfd);
	//3.循环判断
	while(1){
		int ret = epoll_wait(epfd, all_events, MAXSIZE, -1);
		printf("有事件产生\n");
		for(i = 0; i < ret; i++){
			struct epoll_event* ev = &all_events[i];
			if(!(ev->events & EPOLLIN)){
				continue;
			}
			if(ev->data.fd == lfd){
				//处理新的客户端连接
				printf("添加新客户端\n");
				do_accept(lfd, epfd);
			}else{
				//有客户端发送请求
				printf("有客户端的请求\n");
				do_read(ev->data.fd, epfd);
			}
		}
	}

}
int main(int argc,char* argv[])
{
	//命令行参数获取，端口和server提供的目录
	if(argc < 3){
		printf("./a.out port path\n");
		return 1;
	}
	//获取用户输入的端口号
	int port = atoi(argv[1]);
	//改变进程工作目录
	int ret = chdir(argv[2]);
	if(ret != 0){
		perror("chdir error");
		exit(1);
	}
	//启动epoll监听
	epoll_run(port);
	//1.创建树根
	//2.创建lfd，监听事件，并挂到树上监听
	//3.监听事件
	//4.释放资源	
	return 0;
}
