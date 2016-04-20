/*
Esp8266 http server - core routines
*/

/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain 
 * this notice you can do whatever you want with this stuff. If we meet some day, 
 * and you think this stuff is worth it, you can buy me a beer in return. 
 * ----------------------------------------------------------------------------
 */


#include <esp8266.h>

#include "httpd.h"
#include "espfs.h"
#include "debug.h"
#include "tinyprintf.h"


//Max length of request head
#define MAX_HEAD_LEN 1024
//Max amount of connections
#define MAX_CONN 8
//Max post buffer len
#define MAX_POST 1024
//Max send buffer len
#define MAX_SENDBUFF_LEN 2048


//This gets set at init time.
static HttpdBuiltInUrl *builtInUrls;

//Private data for http connection
struct HttpdPriv {
	char head[MAX_HEAD_LEN];
	int headPos;
	int postPos;
	char *sendBuff;
	int sendBuffLen;
};

//Connection pool
static HttpdPriv connPrivData[MAX_CONN];
static HttpdConnData connData[MAX_CONN];

//Listening connection data
static struct espconn httpdConn;
static esp_tcp httpdTcp;

//Struct to keep extension->mime data in
typedef struct {
	const char *ext;
	const char *mimetype;
} MimeMap;

//The mappings from file extensions to mime types. If you need an extra mime type,
//add it here.
static const MimeMap mimeTypes[]={
	{"htm", "text/htm"},
	{"html", "text/html"},
	{"css", "text/css"},
	{"js", "text/javascript"},
	{"txt", "text/plain"},
	{"jpg", "image/jpeg"},
	{"jpeg", "image/jpeg"},
	{"png", "image/png"},
	{NULL, "text/html"}, //default value
};

static os_timer_t httpdDisconnectTimer;

//Returns a static char* to a mime type for a given url to a file.
ICACHE_FLASH_ATTR
const char *httpdGetMimetype(char *url) {
	int i=0;
	//Go find the extension
	char *ext=url+(strlen(url)-1);
	while (ext!=url && *ext!='.') ext--;
	if (*ext=='.') ext++;
	
	//ToDo: os_strcmp is case sensitive; we may want to do case-intensive matching here...
	while (mimeTypes[i].ext!=NULL && os_strcmp(ext, mimeTypes[i].ext)!=0) i++;
	return mimeTypes[i].mimetype;
}

//Looks up the connData info for a specific esp connection
ICACHE_FLASH_ATTR
static HttpdConnData *httpdFindConnData(void *arg) {
	int i;
	for (i=0; i<MAX_CONN; i++) {
		if (connData[i].conn==(struct espconn *)arg) return &connData[i];
	}
	INFO("FindConnData: Huh? Couldn't find connection for %p\n", arg);
	return NULL; //WtF?
}

//Retires a connection for re-use
ICACHE_FLASH_ATTR
static void httpdRetireConn(HttpdConnData *conn) {
	if (conn->postBuff!=NULL) os_free(conn->postBuff);
	conn->postBuff=NULL;
	conn->cgi=NULL;
	conn->conn=NULL;
}

//Stupid li'l helper function that returns the value of a hex char.
static int httpdHexVal(char c) {
	if (c>='0' && c<='9') return c-'0';
	if (c>='A' && c<='F') return c-'A'+10;
	if (c>='a' && c<='f') return c-'a'+10;
	return 0;
}

//Decode a percent-encoded value.
//Takes the valLen bytes stored in val, and converts it into at most retLen bytes that
//are stored in the ret buffer. Returns the actual amount of bytes used in ret. Also
//zero-terminates the ret buffer.
int httpdUrlDecode(char *val, int valLen, char *ret, int retLen) {
	int s=0, d=0;
	int esced=0, escVal=0;
	while (s<valLen && d<retLen) {
		if (esced==1)  {
			escVal=httpdHexVal(val[s])<<4;
			esced=2;
		} else if (esced==2) {
			escVal+=httpdHexVal(val[s]);
			ret[d++]=escVal;
			esced=0;
		} else if (val[s]=='%') {
			esced=1;
		} else if (val[s]=='+') {
			ret[d++]=' ';
		} else {
			ret[d++]=val[s];
		}
		s++;
	}
	if (d<retLen) ret[d]=0;
	return d;
}

//Find a specific arg in a string of get- or post-data.
//Line is the string of post/get-data, arg is the name of the value to find. The
//zero-terminated result is written in buff, with at most buffLen bytes used. The
//function returns the length of the result, or -1 if the value wasn't found. The 
//returned string will be urldecoded already.
ICACHE_FLASH_ATTR
int httpdFindArg(char *line, char *arg, char *buff, int buffLen) {
	char *p, *e;
	if (line==NULL) return 0;
	p=line;
	while(p!=NULL && *p!='\n' && *p!='\r' && *p!=0) {
		INFO("findArg: %s\n", p);
		if (os_strncmp(p, arg, os_strlen(arg))==0 && p[strlen(arg)]=='=') {
			p+=os_strlen(arg)+1; //move p to start of value
			e=(char*)os_strstr(p, "&");
			if (e==NULL) e=p+os_strlen(p);
			INFO("findArg: val %s len %d\n", p, (e-p));
			return httpdUrlDecode(p, (e-p), buff, buffLen);
		}
		p=(char*)os_strstr(p, "&");
		if (p!=NULL) p+=1;
	}
	INFO("Finding %s in %s: Not found :/\n", arg, line);
	return -1; //not found
}

//Get the value of a certain header in the HTTP client head
ICACHE_FLASH_ATTR
int httpdGetHeader(HttpdConnData *conn, char *header, char *ret, int retLen) {
	char *p=conn->priv->head;
	p=p+strlen(p)+1; //skip GET/POST part
	p=p+strlen(p)+1; //skip HTTP part
	while (p<(conn->priv->head+conn->priv->headPos)) {
		while(*p<=32 && *p!=0) p++; //skip crap at start
//		INFO("Looking for %s, Header: '%s'\n", header, p);
		//See if this is the header
		if (os_strncmp(p, header, strlen(header))==0 && p[strlen(header)]==':') {
			//Skip 'key:' bit of header line
			p=p+strlen(header)+1;
			//Skip past spaces after the colon
			while(*p==' ') p++;
			//Copy from p to end
			while (*p!=0 && *p!='\r' && *p!='\n' && retLen>1) {
				*ret++=*p++;
				retLen--;
			}
			//Zero-terminate string
			*ret=0;
			//All done :)
			return 1;
		}
		p+=strlen(p)+1; //Skip past end of string and \0 terminator
	}
	return 0;
}

//Start the response headers.
ICACHE_FLASH_ATTR
void httpdStartResponse(HttpdConnData *conn, int code) {
	char buff[128];
	int l;
	tfp_snprintf(buff, 128, "HTTP/1.0 %d OK\r\nServer: esp8266-httpd/"HTTPDVER"\r\n", code);
	l = strlen(buff);
	httpdSend(conn, buff, l);
}

//Send a http header.
ICACHE_FLASH_ATTR
void httpdHeader(HttpdConnData *conn, const char *field, const char *val) {
	char buff[256];
	int l;

	tfp_snprintf(buff, 256, "%s: %s\r\n", field, val);
	l = strlen(buff);
	httpdSend(conn, buff, l);
}

//Finish the headers.
ICACHE_FLASH_ATTR
void httpdEndHeaders(HttpdConnData *conn) {
	httpdSend(conn, "\r\n", -1);
}

//Redirect to the given URL.
ICACHE_FLASH_ATTR
void httpdRedirect(HttpdConnData *conn, char *newUrl) {
	char buff[1024];
	int l;
	tfp_snprintf(buff, 1024, "HTTP/1.1 302 Found\r\nLocation: %s\r\n\r\nMoved to %s\r\n", newUrl, newUrl);
	l = strlen(buff);
	httpdSend(conn, buff, l);
}

//Use this as a cgi function to redirect one url to another.
ICACHE_FLASH_ATTR
int cgiRedirect(HttpdConnData *connData) {
	if (connData->conn==NULL) {
		//Connection aborted. Clean up.
		return HTTPD_CGI_DONE;
	}
	httpdRedirect(connData, (char*)connData->cgiArg);
	return HTTPD_CGI_DONE;
}


//Add data to the send buffer. len is the length of the data. If len is -1
//the data is seen as a C-string.
//Returns 1 for success, 0 for out-of-memory.
ICACHE_FLASH_ATTR
int httpdSend(HttpdConnData *conn, const char *data, int len) {
	if (len<0) len=strlen(data);
	if (conn->priv->sendBuffLen+len>MAX_SENDBUFF_LEN) return 0;
	os_memcpy(conn->priv->sendBuff+conn->priv->sendBuffLen, data, len);
	conn->priv->sendBuffLen+=len;
	return 1;
}

//Helper function to send any data in conn->priv->sendBuff
ICACHE_FLASH_ATTR
static void xmitSendBuff(HttpdConnData *conn) {
	if (conn->priv->sendBuffLen!=0) {
		espconn_sent(conn->conn, (uint8_t*)conn->priv->sendBuff, conn->priv->sendBuffLen);
		conn->priv->sendBuffLen=0;
	}
}

//Callback called when the data on a socket has been successfully
//sent.
ICACHE_FLASH_ATTR
static void httpdSentCb(void *arg) {
	int r;
	HttpdConnData *conn=httpdFindConnData(arg);
	char sendBuff[MAX_SENDBUFF_LEN];

//	INFO("Sent callback on conn %p\n", conn);
	if (conn==NULL) return;
	conn->priv->sendBuff=sendBuff;
	conn->priv->sendBuffLen=0;

	if (conn->cgi==NULL) { //Marked for destruction?
		INFO("Conn %p is done. Closing.\n", conn->conn);
		espconn_disconnect(conn->conn);
		httpdRetireConn(conn);
		return; //No need to call xmitSendBuff.
	}

	r=conn->cgi(conn); //Execute cgi fn.
	if (r==HTTPD_CGI_DONE) {
		conn->cgi=NULL; //mark for destruction.
	}
	xmitSendBuff(conn);
}

static const char *httpNotFoundHeader="HTTP/1.0 404 Not Found\r\nServer: esp8266-httpd/0.1\r\nContent-Type: text/plain\r\n\r\nNot Found.\r\n";

//This is called when the headers have been received and the connection is ready to send
//the result headers and data.
ICACHE_FLASH_ATTR
static void httpdSendResp(HttpdConnData *conn) {
	int i=0;
	int r;
	//See if the url is somewhere in our internal url table.
	while (builtInUrls[i].url!=NULL && conn->url!=NULL) {
		int match=0;
//		INFO("%s == %s?\n", builtInUrls[i].url, conn->url);
		if (os_strcmp(builtInUrls[i].url, conn->url)==0) match=1;
		if (builtInUrls[i].url[os_strlen(builtInUrls[i].url)-1]=='*' &&
				os_strncmp(builtInUrls[i].url, conn->url, os_strlen(builtInUrls[i].url)-1)==0) match=1;
		if (match) {
			INFO("Is url index %d\n", i);
			conn->cgiData=NULL;
			conn->cgi=builtInUrls[i].cgiCb;
			conn->cgiArg=builtInUrls[i].cgiArg;
			r=conn->cgi(conn);
			if (r!=HTTPD_CGI_NOTFOUND) {
				if (r==HTTPD_CGI_DONE) conn->cgi=NULL;  //If cgi finishes immediately: mark conn for destruction.
				return;
			}
		}
		i++;
	}
	//Can't find :/
	INFO("%s not found. 404!\n", conn->url);
	httpdSend(conn, httpNotFoundHeader, -1);
	conn->cgi=NULL; //mark for destruction
}

//Parse a line of header data and modify the connection data accordingly.
ICACHE_FLASH_ATTR
static void httpdParseHeader(char *h, HttpdConnData *conn) {
	int i;
//	INFO("Got header %s\n", h);
	if (os_strncmp(h, "GET ", 4)==0 || os_strncmp(h, "POST ", 5)==0) {
		char *e;
		
		//Skip past the space after POST/GET
		i=0;
		while (h[i]!=' ') i++;
		conn->url=h+i+1;

		//Figure out end of url.
		e=(char*)os_strstr(conn->url, " ");
		if (e==NULL) return; //wtf?
		*e=0; //terminate url part

		INFO("URL = %s\n", conn->url);
		//Parse out the URL part before the GET parameters.
		conn->getArgs=(char*)os_strstr(conn->url, "?");
		if (conn->getArgs!=0) {
			*conn->getArgs=0;
			conn->getArgs++;
			INFO("GET args = %s\n", conn->getArgs);
		} else {
			conn->getArgs=NULL;
		}
	} else if (os_strncmp(h, "Content-Length: ", 16)==0) {
		i=0;
		//Skip trailing spaces
		while (h[i]!=' ') i++;
		//Get POST data length
		conn->postLen=atoi(h+i+1);
		//Clamp if too big. Hmm, maybe we should error out instead?
		if (conn->postLen>MAX_POST) conn->postLen=MAX_POST;
		INFO("Mallocced buffer for %d bytes of post data.\n", conn->postLen);
		//Alloc the memory.
		conn->postBuff=(char*)os_malloc(conn->postLen+1);
		conn->priv->postPos=0;
	}
}


//Callback called when there's data available on a socket.
ICACHE_FLASH_ATTR
static void httpdRecvCb(void *arg, char *data, unsigned short len) {
	int x;
	char *p, *e;
	char sendBuff[MAX_SENDBUFF_LEN];
	HttpdConnData *conn=httpdFindConnData(arg);
	if (conn==NULL) return;
	conn->priv->sendBuff=sendBuff;
	conn->priv->sendBuffLen=0;

	for (x=0; x<len; x++) {
		if (conn->postLen<0) {
			//This byte is a header byte.
			if (conn->priv->headPos!=MAX_HEAD_LEN) conn->priv->head[conn->priv->headPos++]=data[x];
			conn->priv->head[conn->priv->headPos]=0;
			//Scan for /r/n/r/n
			if (data[x]=='\n' && (char *)os_strstr(conn->priv->head, "\r\n\r\n")!=NULL) {
				//Indicate we're done with the headers.
				conn->postLen=0;
				//Reset url data
				conn->url=NULL;
				//Find end of next header line
				p=conn->priv->head;
				while(p<(&conn->priv->head[conn->priv->headPos-4])) {
					e=(char *)os_strstr(p, "\r\n");
					if (e==NULL) break;
					e[0]=0;
					httpdParseHeader(p, conn);
					p=e+2;
				}
				//If we don't need to receive post data, we can send the response now.
				if (conn->postLen==0) {
					httpdSendResp(conn);
				}
			}
		} else if (conn->priv->postPos!=-1 && conn->postLen!=0 && conn->priv->postPos <= conn->postLen) {
			//This byte is a POST byte.
			conn->postBuff[conn->priv->postPos++]=data[x];
			if (conn->priv->postPos>=conn->postLen) {
				//Received post stuff.
				conn->postBuff[conn->priv->postPos]=0; //zero-terminate
				conn->priv->postPos=-1;
				INFO("Post data: %s\n", conn->postBuff);
				//Send the response.
				httpdSendResp(conn);
				break;
			}
		}
	}
	xmitSendBuff(conn);
}

ICACHE_FLASH_ATTR
static void httpdReconCb(void *arg, sint8 err) {
	HttpdConnData *conn=httpdFindConnData(arg);
	INFO("ReconCb\n");
	if (conn==NULL) return;
	//Yeah... No idea what to do here. ToDo: figure something out.
}

ICACHE_FLASH_ATTR
static void httpdDisconCb(void *arg) {
#if 0
	//Stupid esp sdk passes through wrong arg here, namely the one of the *listening* socket.
	//If it ever gets fixed, be sure to update the code in this snippet; it's probably out-of-date.
	HttpdConnData *conn=httpdFindConnData(arg);
	INFO("Disconnected, conn=%p\n", conn);
	if (conn==NULL) return;
	conn->conn=NULL;
	if (conn->cgi!=NULL) conn->cgi(conn); //flush cgi data
#endif
	//Just look at all the sockets and kill the slot if needed.
	int i;
	for (i=0; i<MAX_CONN; i++) {
		if (connData[i].conn!=NULL) {
			//Why the >=ESPCONN_CLOSE and not ==? Well, seems the stack sometimes de-allocates
			//espconns under our noses, especially when connections are interrupted. The memory
			//is then used for something else, and we can use that to capture *most* of the
			//disconnect cases.
			if (connData[i].conn->state==ESPCONN_NONE || connData[i].conn->state>=ESPCONN_CLOSE) {
				connData[i].conn=NULL;
				if (connData[i].cgi!=NULL) connData[i].cgi(&connData[i]); //flush cgi data
				httpdRetireConn(&connData[i]);
			}
		}
	}
}


ICACHE_FLASH_ATTR
static void httpdConnectCb(void *arg) {
	struct espconn *conn=arg;
	int i;
	//Find empty conndata in pool
	for (i=0; i<MAX_CONN; i++) if (connData[i].conn==NULL) break;
	INFO("Con req, conn=%p, pool slot %d\n", conn, i);
	if (i==MAX_CONN) {
		INFO("Aiee, conn pool overflow!\n");
		espconn_disconnect(conn);
		return;
	}
	connData[i].priv=&connPrivData[i];
	connData[i].conn=conn;
	connData[i].priv->headPos=0;
	connData[i].postBuff=NULL;
	connData[i].priv->postPos=0;
	connData[i].postLen=-1;

	espconn_regist_recvcb(conn, httpdRecvCb);
	espconn_regist_reconcb(conn, httpdReconCb);
	espconn_regist_disconcb(conn, httpdDisconCb);
	espconn_regist_sentcb(conn, httpdSentCb);
}


ICACHE_FLASH_ATTR
void httpdInit(HttpdBuiltInUrl *fixedUrls, int port) {
	int i;

	for (i=0; i<MAX_CONN; i++) {
		connData[i].conn=NULL;
	}
	httpdConn.type=ESPCONN_TCP;
	httpdConn.state=ESPCONN_NONE;
	httpdTcp.local_port=port;
	httpdConn.proto.tcp=&httpdTcp;
	builtInUrls=fixedUrls;

	INFO("Httpd init, conn=%p\n", &httpdConn);
	espconn_regist_connectcb(&httpdConn, httpdConnectCb);
	espconn_accept(&httpdConn);
}

ICACHE_FLASH_ATTR
void static httpdDisconnectTimerFunc(void *arg) {
	httpdStop();
}

ICACHE_FLASH_ATTR
void httpdStop() {
	INFO("Httpd stopping\n");
	
	if (espconn_delete(&httpdConn) == 0) {
	    os_timer_disarm(&httpdDisconnectTimer);
		httpdRetireConn(connData);
		INFO("Httpd stopped\n");
	}
	else {
		espconn_disconnect(&httpdConn);
	    os_timer_disarm(&httpdDisconnectTimer);
	    os_timer_setfn(&httpdDisconnectTimer, (os_timer_func_t *)httpdDisconnectTimerFunc, NULL);
	    os_timer_arm(&httpdDisconnectTimer, 1000, 0);
		INFO("Httpd still running rescheduling httpdStop\n");
	}
}
