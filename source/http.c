#include <net/net.h>
#include <sysmodule/sysmodule.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <curl/curl.h>

#include "common.h"
#include "saves.h"

#define HTTP_SUCCESS 	1
#define HTTP_FAILED	 	0

#define HTTP_USER_AGENT "Mozilla/5.0 (PLAYSTATION 3; 1.00)"

uint64_t prog_bar1_value=0;


int http_init(void)
{
    int ret;

	//init
	ret = sysModuleLoad(SYSMODULE_NET);
	if (ret < 0) {
		LOG("Error : sysModuleLoad(SYSMODULE_NET) HTTP_FAILED (%x)", ret);
		return HTTP_FAILED;
	}

	ret = netInitialize();
	if (ret < 0) {
		LOG("Error : netInitialize HTTP_FAILED (%x)", ret);
		return HTTP_FAILED;
	}

	curl_global_init(CURL_GLOBAL_ALL);

	return HTTP_SUCCESS;
}

void http_end(void)
{
	curl_global_cleanup();
//	netDeinitialize();
	sysModuleUnload(SYSMODULE_NET);

	return;
}

/* follow the CURLOPT_XFERINFOFUNCTION callback definition */
static int update_progress(void *p, int64_t dltotal, int64_t dlnow, int64_t ultotal, int64_t ulnow)
{
    LOG("DL: %lld / %lld", dlnow, dltotal);
	update_progress_bar(&prog_bar1_value, dltotal, (const char*) p);
    return 0;
}

static size_t curl_write_file(void *contents, size_t size, size_t nmemb, void *userp)
{
    prog_bar1_value += (size * nmemb);

	return fwrite(contents, size, nmemb, userp);
}

int http_download(const char* url, const char* filename, const char* local_dst, int show_progress)
{
	char full_url[1024];
	CURL *curl;
	CURLcode res;
	FILE* fd;
	prog_bar1_value = 0;

	curl = curl_easy_init();
	if(!curl)
	{
		LOG("ERROR: CURL INIT");
		return HTTP_FAILED;
	}

	fd = fopen(local_dst, "wb");
	if (!fd) {
		LOG("Error : fopen() HTTP_FAILED : %s", local_dst);
		return HTTP_FAILED;
	}

	snprintf(full_url, sizeof(full_url), "%s%s", url, filename);
	LOG("Downloading (%s) -> (%s)", full_url, local_dst);

	curl_easy_setopt(curl, CURLOPT_URL, full_url);
	// Set user agent string
	curl_easy_setopt(curl, CURLOPT_USERAGENT, HTTP_USER_AGENT);
	// not sure how to use this when enabled
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	// not sure how to use this when enabled
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	// Set SSL VERSION to TLS 1.2
	curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
	// Set timeout for the connection to build
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
	// Follow redirects
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	// The function that will be used to write the data 
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &curl_write_file);
	// The data filedescriptor which will be written to
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fd);
	// maximum number of redirects allowed
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 20L);
	// Fail the request if the HTTP code returned is equal to or larger than 400
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
	// request using SSL for the FTP transfer if available
	curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_TRY);

	if (show_progress)
	{
		init_progress_bar("Downloading...", filename);
		/* pass the struct pointer into the xferinfo function */
		curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, &update_progress);
		curl_easy_setopt(curl, CURLOPT_XFERINFODATA, (void*) full_url);
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	}

	// Perform the request
	res = curl_easy_perform(curl);
	// close filedescriptor
	fclose(fd);
	// cleanup
	curl_easy_cleanup(curl);

	if (show_progress)
		end_progress_bar();

	if(res != CURLE_OK)
	{
		LOG("curl_easy_perform() failed: %s", curl_easy_strerror(res));
		unlink_secure(local_dst);
		return HTTP_FAILED;
	}

	return HTTP_SUCCESS;
}
