#include "HttpConnectionWinHttp.h"

// WinHttp ライブラリをリンク
#pragma comment(lib, "winhttp.lib")

static HINTERNET
detachHandle(HINTERNET &handle)
{
	return (HINTERNET)::InterlockedExchangePointer(
		reinterpret_cast<PVOID volatile *>(&handle), NULL);
}

static void
closeDetachedHandle(HINTERNET &handle)
{
	HINTERNET detached = detachHandle(handle);
	if (detached) {
		WinHttpCloseHandle(detached);
	}
}

//////////////////////////////////////////////////////////////////////////
// クラス外で利用するヘルパー等がある場合はここに追加
//////////////////////////////////////////////////////////////////////////

static bool parseUrl(const tstring &url,
					 tstring &host,
					 INTERNET_PORT &port,
					 tstring &path,
					 bool &secure,
					 tstring &user,
					 tstring &passwd)
{
	URL_COMPONENTS uc;
	ZeroMemory(&uc, sizeof uc);
	uc.dwStructSize = sizeof uc;
	uc.dwSchemeLength   = 1;
	uc.dwHostNameLength = 1;
	uc.dwUserNameLength = 1;
	uc.dwPasswordLength = 1;
	uc.dwUrlPathLength  = 1;
	uc.dwExtraInfoLength = 1;

	if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) {
		return false;
	}

	if (uc.nScheme != INTERNET_SCHEME_HTTP && uc.nScheme != INTERNET_SCHEME_HTTPS) {
		return false;
	}

	secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
	port = uc.nPort;
	host.clear();
	path.clear();
	user.clear();
	passwd.clear();

	if (uc.lpszHostName && uc.dwHostNameLength > 0) {
		host = tstring(uc.lpszHostName, uc.dwHostNameLength);
	}
	if (host.empty()) {
		return false;
	}

	if (uc.lpszUrlPath && uc.dwUrlPathLength > 0) {
		path = tstring(uc.lpszUrlPath, uc.dwUrlPathLength);
	}
	if (path.empty()) {
		path = _T("/");
	}
	if (uc.lpszExtraInfo && uc.dwExtraInfoLength > 0) {
		path += tstring(uc.lpszExtraInfo, uc.dwExtraInfoLength);
	}
	if (uc.lpszUserName && uc.dwUserNameLength > 0) {
		user = tstring(uc.lpszUserName, uc.dwUserNameLength);
	}
	if (uc.lpszPassword && uc.dwPasswordLength > 0) {
		passwd = tstring(uc.lpszPassword, uc.dwPasswordLength);
	}

	return true;
}


//////////////////////////////////////////////////////////////////////////
// HttpConnectionWinHttp 実装
//////////////////////////////////////////////////////////////////////////

void HttpConnectionWinHttp::closeHandle()
{
	::EnterCriticalSection(&cs);
	closeDetachedHandle(hRequest);
	closeDetachedHandle(hConnect);
	closeDetachedHandle(hSession);
	::LeaveCriticalSection(&cs);
}

void HttpConnectionWinHttp::abortRequest()
{
	closeDetachedHandle(hRequest);
}


bool HttpConnectionWinHttp::open(const TCHAR *method,
								 const TCHAR *url,
								 const TCHAR *user,
								 const TCHAR *passwd)
{
	::EnterCriticalSection(&cs);
	clearParam();
	errorMessage.clear();

	// URL分解
	tstring hostName, urlPath, urlUser, urlPasswd;
	INTERNET_PORT port;
	bool result = parseUrl(url, hostName, port, urlPath, secure, urlUser, urlPasswd);
	if (!result) {
		errorMessage = _T("Invalid URL");
		::LeaveCriticalSection(&cs);
		return false;
	}

	// WinHttpOpen (セッション生成)
	hSession = WinHttpOpen(agentName.c_str(),
						   WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
						   WINHTTP_NO_PROXY_NAME,
						   WINHTTP_NO_PROXY_BYPASS,
						   0);
	if (!hSession) {
		errorMessage = _T("WinHttpOpen failed");
		::LeaveCriticalSection(&cs);
		return false;
	}

	// WinHttpConnect (接続生成)
	hConnect = WinHttpConnect(hSession, hostName.c_str(), port, 0);
	if (!hConnect) {
		errorMessage = _T("WinHttpConnect failed");
		::LeaveCriticalSection(&cs);
		return false;
	}

	// WinHttpOpenRequest
	DWORD flags = 0;
	if (secure) {
		flags |= WINHTTP_FLAG_SECURE;
	}

	hRequest = WinHttpOpenRequest(hConnect,
								  method,
								  urlPath.c_str(),
								  NULL,
								  WINHTTP_NO_REFERER,
								  WINHTTP_DEFAULT_ACCEPT_TYPES,
								  flags);
	if (!hRequest) {
		errorMessage = _T("WinHttpOpenRequest failed");
		::LeaveCriticalSection(&cs);
		return false;
	}

	// 証明書チェックを行わない場合は、オプションをセットして無視できるようにする
	if (!checkCert && secure) {
		DWORD dwSecFlags =
			SECURITY_FLAG_IGNORE_UNKNOWN_CA |
			SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
			SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
			SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
		WinHttpSetOption(hRequest,
						 WINHTTP_OPTION_SECURITY_FLAGS,
						 &dwSecFlags,
						 sizeof(dwSecFlags));
	}

	// 認証を行う場合 (Basic 認証など) が必要なら、WinHttpSetCredentials を呼ぶ方法もあるが、
	// addBasicAuthHeader() などで Authorization ヘッダを追加する実装と両立させる場合は注意。
	// ここでは従来のヘッダ追加スタイルを維持するために、特に実装はしません。
	tstring authUser;
	if (user && user[0] != _T('\0')) {
		authUser = user;
	}
	if (!urlUser.empty()) {
		authUser = urlUser;
	}
	tstring authPasswd;
	if (passwd) {
		authPasswd = passwd;
	}
	if (!urlPasswd.empty()) {
		authPasswd = urlPasswd;
	}
	if (!authUser.empty()) {
		// Basic認証用のヘッダを生成
		// 例: "Authorization: Basic <Base64>"
		addBasicAuthHeader(authUser, authPasswd);
	}

	::LeaveCriticalSection(&cs);
	return true;
}

int HttpConnectionWinHttp::request(RequestCallback requestCallback,
								   RetryCallback retryCallback,
								   void *context)
{
	::EnterCriticalSection(&cs);
	if (!hRequest) {
		errorMessage = _T("Invalid request handle");
		::LeaveCriticalSection(&cs);
		return ERROR_INET;
	}

	// 送信ヘッダを合体
	tstring allHeaders;
	for (size_t i = 0; i < header.size(); i++) {
		allHeaders += header[i];
		allHeaders += _T("\r\n");
	}

	// WinHttpSendRequest では、追加ヘッダと送信データポインタを直接指定可能
	// (コールバックで送りたい場合は、後で WinHttpWriteData を利用して送る事も可能)
	BOOL bRet = WinHttpSendRequest(
								   hRequest,
								   (allHeaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : allHeaders.c_str()),
								   0,
								   WINHTTP_NO_REQUEST_DATA,	 // まずは何も送らず、後からWriteする想定
								   0,
								   (requestContentLength ? requestContentLength : 0),
								   0);
	if (!bRet) {
		errorMessage = _T("WinHttpSendRequest failed");
		::LeaveCriticalSection(&cs);
		return ERROR_INET;
	}

	// もし RequestCallback があるなら、WinHttpWriteData を用いて送信する
	if (requestCallback) {
		// コールバックからデータを受け取りつつ書き込む
		char buffer[4096];
		DWORD size = 0;
		while (true) {
			size = sizeof(buffer);
			bool isCancel = ! requestCallback(context, buffer, size);
			if (isCancel) {
				// ユーザ都合でキャンセル
				closeDetachedHandle(hRequest);
				::LeaveCriticalSection(&cs);
				return ERROR_CANCEL;
			}
			if (size == 0) {
				// データが尽きた
				break;
			}
			DWORD written = 0;
			BOOL wres = WinHttpWriteData(hRequest, buffer, size, &written);
			if (!wres || (written != size)) {
				errorMessage = _T("WinHttpWriteData failed");
				::LeaveCriticalSection(&cs);
				return ERROR_INET;
			}
		}
	}

	// 実際にリクエストを確定(受信開始できる状態にする)
	BOOL bEnd = WinHttpReceiveResponse(hRequest, NULL);
	if (!bEnd) {
		errorMessage = _T("WinHttpReceiveResponse failed");
		::LeaveCriticalSection(&cs);
		return ERROR_INET;
	}

	::LeaveCriticalSection(&cs);
	return ERROR_NONE;
}

void HttpConnectionWinHttp::queryInfo()
{
	::EnterCriticalSection(&cs);

	validContentLength = false;
	contentLength = 0;
	contentType.clear();
	encoding.clear();
	statusCode = 0;
	statusText.clear();
	responseHeaders.clear();

	if (!hRequest) {
		::LeaveCriticalSection(&cs);
		return;
	}

	// HTTP ステータスコードを取得
	{
		DWORD statusCodeSize = sizeof(statusCode);
		WinHttpQueryHeaders(hRequest,
							WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
							WINHTTP_HEADER_NAME_BY_INDEX,
							&statusCode,
							&statusCodeSize,
							WINHTTP_NO_HEADER_INDEX);
	}

	// HTTP ステータステキストを取得
	{
		DWORD dwSize = 0;
		WinHttpQueryHeaders(hRequest,
							WINHTTP_QUERY_STATUS_TEXT,
							WINHTTP_HEADER_NAME_BY_INDEX,
							WINHTTP_NO_OUTPUT_BUFFER,
							&dwSize,
							WINHTTP_NO_HEADER_INDEX);
		if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && dwSize > 0) {
			TCHAR *buf = new TCHAR[dwSize/sizeof(TCHAR)];
			if (WinHttpQueryHeaders(hRequest,
									WINHTTP_QUERY_STATUS_TEXT,
									WINHTTP_HEADER_NAME_BY_INDEX,
									buf,
									&dwSize,
									WINHTTP_NO_HEADER_INDEX))
				{
					statusText.assign(buf);
				}
			delete [] buf;
		}
	}

	// Content-Length 取得
	{
		DWORD dwSize = sizeof(contentLength);
		if (WinHttpQueryHeaders(hRequest,
								WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
								NULL,
								&contentLength,
								&dwSize,
								WINHTTP_NO_HEADER_INDEX))
			{
				validContentLength = true;
			}
	}

	// Content-Type 取得
	{
		DWORD dwSize = 0;
		WinHttpQueryHeaders(hRequest,
							WINHTTP_QUERY_CONTENT_TYPE,
							NULL,
							WINHTTP_NO_OUTPUT_BUFFER,
							&dwSize,
							WINHTTP_NO_HEADER_INDEX);
		if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && dwSize > 0) {
			TCHAR *buf = new TCHAR[dwSize/sizeof(TCHAR)];
			if (WinHttpQueryHeaders(hRequest,
									WINHTTP_QUERY_CONTENT_TYPE,
									NULL,
									buf,
									&dwSize,
									WINHTTP_NO_HEADER_INDEX))
				{
					// Content-Type: text/html; charset=UTF-8 のような例
					// ";" より前を contentType, 後ろを encoding として分離
					tstring ct(buf);
					tstring::size_type pos = ct.find(_T(";"));
					if (pos == tstring::npos) {
						contentType = ct;
					} else {
						contentType = ct.substr(0, pos);
						// charset= 等を取得
						tstring sub = ct.substr(pos + 1);
						// "charset=" という文字列がある場合に抜き出す
						tstring::size_type cpos = sub.find(_T("charset="));
						if (cpos != tstring::npos) {
							// charset= のあとをトリムして encoding にする
							encoding = sub.substr(cpos + 8);
							// 先頭の空白などを除去したければ適宜実装
						}
					}
				}
			delete [] buf;
		}
	}

	// 生ヘッダ全体を取得し、レスポンスヘッダをパース
	{
		DWORD dwSize = 0;
		WinHttpQueryHeaders(hRequest,
							WINHTTP_QUERY_RAW_HEADERS_CRLF,
							WINHTTP_HEADER_NAME_BY_INDEX,
							WINHTTP_NO_OUTPUT_BUFFER,
							&dwSize,
							WINHTTP_NO_HEADER_INDEX);

		if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && dwSize > 0) {
			TCHAR *buf = new TCHAR[dwSize/sizeof(TCHAR)];
			if (WinHttpQueryHeaders(hRequest,
									WINHTTP_QUERY_RAW_HEADERS_CRLF,
									WINHTTP_HEADER_NAME_BY_INDEX,
									buf,
									&dwSize,
									WINHTTP_NO_HEADER_INDEX))
				{
					// ヘッダ全体を行単位に分割
					// 1行目はステータスなどが含まれているため除外したい場合は適宜カット
					// 今回は全て保持しつつ、name:value 形式だけを map に入れる例
					tstring headers = buf;
					size_t start = 0;
					while (true) {
						size_t end = headers.find(_T("\r\n"), start);
						if (end == tstring::npos) {
							break;
						}
						tstring line = headers.substr(start, end - start);
						start = end + 2; // skip "\r\n"

						// たとえば "Content-Type: text/html" 形式を解析
						tstring::size_type colonPos = line.find(_T(":"));
						if (colonPos != tstring::npos) {
							tstring key = line.substr(0, colonPos);
							tstring val = line.substr(colonPos + 1);
							// 前後空白を適宜trimして格納
							// ここでは最低限の処理のみ
							while (!val.empty() && (val[0] == _T(' ') || val[0] == _T('\t'))) {
								val.erase(val.begin());
							}
							responseHeaders[key] = val;
						}
					}
				}
			delete [] buf;
		}
	}

	::LeaveCriticalSection(&cs);
}

int HttpConnectionWinHttp::response(ResponseCallback callback, void *context)
{
	::EnterCriticalSection(&cs);
	if (!hRequest) {
		errorMessage = _T("Invalid request handle");
		::LeaveCriticalSection(&cs);
		return ERROR_INET;
	}

	// レスポンスを読み出す
	const DWORD BUF_SIZE = 4096;
	char buffer[BUF_SIZE];

	while (true) {
		DWORD dwRead = 0;
		BOOL bRes = WinHttpReadData(hRequest, buffer, BUF_SIZE, &dwRead);
		if (!bRes) {
			errorMessage = _T("WinHttpReadData failed");
			::LeaveCriticalSection(&cs);
			return ERROR_INET;
		}
		if (dwRead == 0) {
			// 受信完了
			break;
		}

		// コールバックに受け渡し
		if (callback) {
			bool isCancel = ! callback(context, buffer, dwRead);
			if (isCancel) {
				::LeaveCriticalSection(&cs);
				return ERROR_CANCEL;
			}
		}
	}

	// 最後に NULL / size=0 で通知
	if (callback) {
		callback(context, NULL, 0);
	}

	::LeaveCriticalSection(&cs);
	return ERROR_NONE;
}
