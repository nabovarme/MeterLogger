#ifndef HTTPD_H
#define HTTPD_H
#include <c_types.h>
#include <ip_addr.h>
#include <espconn.h>

#define HTTPDVER "0.2"

#define HTTPD_CGI_MORE 0
#define HTTPD_CGI_DONE 1
#define HTTPD_CGI_NOTFOUND 2
#define HTTPD_CGI_AUTHENTICATED 2 //for now

typedef struct HttpdPriv HttpdPriv;
typedef struct HttpdConnData HttpdConnData;

typedef int (* cgiSendCallback)(HttpdConnData *connData);

//A struct describing a http connection. This gets passed to cgi functions.
struct HttpdConnData {
	struct espconn *conn;
	char *url;
	char *getArgs;
	const void *cgiArg;
	void *cgiData;
	HttpdPriv *priv;
	cgiSendCallback cgi;
	int postLen;
	char *postBuff;
};

//A struct describing an url. This is the main struct that's used to send different URL requests to
//different routines.
typedef struct {
	const char *url;
	cgiSendCallback cgiCb;
	const void *cgiArg;
} HttpdBuiltInUrl;

ICACHE_FLASH_ATTR int cgiRedirect(HttpdConnData *connData);
ICACHE_FLASH_ATTR void httpdRedirect(HttpdConnData *conn, char *newUrl);
int httpdUrlDecode(char *val, int valLen, char *ret, int retLen);
ICACHE_FLASH_ATTR int httpdFindArg(char *line, char *arg, char *buff, int buffLen);
ICACHE_FLASH_ATTR void httpdInit(HttpdBuiltInUrl *fixedUrls, int port);
const char *httpdGetMimetype(char *url);
ICACHE_FLASH_ATTR void httpdStartResponse(HttpdConnData *conn, int code);
ICACHE_FLASH_ATTR void httpdHeader(HttpdConnData *conn, const char *field, const char *val);
ICACHE_FLASH_ATTR void httpdEndHeaders(HttpdConnData *conn);
ICACHE_FLASH_ATTR int httpdGetHeader(HttpdConnData *conn, char *header, char *ret, int retLen);
ICACHE_FLASH_ATTR int httpdSend(HttpdConnData *conn, const char *data, int len);
ICACHE_FLASH_ATTR void httpdStop();
#endif
