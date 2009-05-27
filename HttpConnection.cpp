#include "HttpConnection.h"
#pragma comment(lib, "Wininet.lib") 

#define BUFSIZE (1024*16)

// �f�[�^������ META�^�O�� Content-Type ���擾����
// �����K�\����z�肵�Ă�̂ɒ���
extern bool matchContentType(tstring &text, tstring &result);

/**
 * �G���[���b�Z�[�W���i�[����
 */
static void
storeErrorMessage(DWORD error, tstring &errorMessage)
{
	TCHAR msg[1024];
	if (FormatMessage(FORMAT_MESSAGE_FROM_HMODULE,
					  GetModuleHandle(_T("wininet.dll")),
					  error,
					  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // ����̌���
					  msg,
					  sizeof msg,
					  NULL
					  ) ||
		FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM,
					  NULL,
					  error,
					  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // ����̌���
					  msg,
					  sizeof msg,
					  NULL
					  )) {
		errorMessage = msg;
	} else {
		errorMessage = _T("unknown error");
	}
}

/**
 * Content-Type ���p�[�X���� ContentType �� encoding �w����擾
 * @param buf �o�b�t�@
 * @param length �o�b�t�@�T�C�Y
 * @param contentType �擾���� Content-Type ���i�[
 * @param encoding �擾�����G���R�[�h�w����i�[
 */
static void
parseContentType(const TCHAR *buf, size_t length, tstring &contentType, tstring &encoding)
{
	// ���̃X�y�[�X��ǂݔ�΂�
	while (_istspace(*buf)) {
		buf++;
		length--;
	}
	size_t n = 0;
	const TCHAR *p;
	if ((p = _tcschr(buf, ';'))) {
		size_t l = p - buf;
		while (n < l && !_istspace(buf[n])) {
			n++;
		}
		contentType = tstring(buf, n);
		n = l+1;
		while (_istspace(buf[n])) n++;
		if (_tcsnicmp(buf+n, _T("charset"), 7) == 0) {
			n += 7;
			while (_istspace(buf[n])) n++;
			if (buf[n] == '=') {
				n++;
				while (_istspace(buf[n])) n++;
				int l = 0;
				while (n+l < length && buf[n+l] && !_istspace(buf[n+l])) l++;
				encoding = tstring(buf+n, l);
			}
		}
	} else {
		while (n < length && buf[n] && !_istspace(buf[n])) {
			n++;
		}
		contentType = tstring(buf, n);
	}
}

void
HttpConnection::addHeader(const TCHAR *name, const TCHAR *value)
{
	if (_tcsicmp(name, _T("Content-Type")) == 0) {
		parseContentType(value, _tcslen(value), requestContentType, requestEncoding);
	}
	tstring n = name;
	n += _T(":");
	n += value;
	header.push_back(n);
}

/**
 * �R�l�N�V�����ڑ��J�n
 */
bool
HttpConnection::open(const TCHAR *method,
					 const TCHAR *url,
					 const TCHAR *_user,
					 const TCHAR *_passwd) {
	clearParam();
	errorMessage.resize(0);

	URL_COMPONENTS uc;
	ZeroMemory(&uc, sizeof uc);
	uc.dwStructSize = sizeof uc;
	uc.dwSchemeLength   = 1;
	uc.dwHostNameLength = 1;
	uc.dwUserNameLength = 1;
	uc.dwPasswordLength = 1;
	uc.dwUrlPathLength  = 1;

	if (!InternetCrackUrl(url, 0, 0, &uc)) {
		storeErrorMessage(GetLastError(), errorMessage);
		return false;
	}

	if (uc.nScheme != INTERNET_SCHEME_HTTP && uc.nScheme != INTERNET_SCHEME_HTTPS) {
		errorMessage = _T("invalid protocol");
		return false;
	}
	
	secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
	tstring host;
	if (uc.lpszHostName) {
		host = tstring(uc.lpszHostName, uc.dwHostNameLength);
	}
	int port = uc.nPort;
	tstring user;
	if (_user) {
		user = _user;
	}
	if (uc.lpszUserName) {
		user = tstring(uc.lpszUserName, uc.dwUserNameLength);
	} else if (_user) {
		user = _user;
	}
	tstring passwd;
	if (uc.lpszPassword) {
		passwd = tstring(uc.lpszPassword, uc.dwPasswordLength);
	} else if (_passwd) {
		passwd = _passwd;
	}
	tstring path;
	if (uc.lpszUrlPath) {
		path = tstring(uc.lpszUrlPath);
	}
	
	bool errorProxyFirst = true;
retry:
	// Internet�ɐڑ�����
	if ((hInet = InternetOpen(agentName.c_str(),
			errorProxyFirst ? INTERNET_OPEN_TYPE_PRECONFIG : INTERNET_OPEN_TYPE_PRECONFIG_WITH_NO_AUTOPROXY, 
			NULL, NULL, 0)) == NULL) {
		storeErrorMessage(GetLastError(), errorMessage);
		return false;
	}

	// HTTP�T�[�o�[�ɐڑ�
	if ((hConn = InternetConnect(hInet, 
								 host.c_str(),
								 port,									// �|�[�g
								 user.size() ? user.c_str() : NULL,		// username
								 passwd.size() ? passwd.c_str() : NULL,	// password
								 INTERNET_SERVICE_HTTP,
								 0, NULL)) == NULL) {
		DWORD error = GetLastError();
		closeHandle();

		if (errorProxyFirst) {
			errorProxyFirst = false;
			goto retry;
		}

		storeErrorMessage(error, errorMessage);
		return false;
	}

	// �T�[�o�[��ŗ~����URL���w�肷��
	if ((hReq = HttpOpenRequest(hConn,
								method,
								path.c_str(),
								NULL, // �f�t�H���g��HTTP�o�[�W����
								NULL, // ������ǉ����Ȃ�
								NULL, // AcceptType
								secure ? INTERNET_FLAG_SECURE : 0, NULL)) == NULL) {

		storeErrorMessage(GetLastError(), errorMessage);
		closeHandle();
		return false;
	}

	// �F�؃w�b�_�ǉ�
	if (user.size() > 0 && passwd.size() > 0) {
		addBasicAuthHeader(user, passwd);
	}

	return true;
}

/**
 * ���N�G�X�g���M
 */
bool
HttpConnection::request(const BYTE *data, int dataSize)
{
	if (!isValid()) {
		return false;
	}
	
	// HTTP �w�b�_������ꍇ�͂����ǉ�����
	if (header.size()) {
		vector<tstring>::iterator it = header.begin();
		while (it != header.end()) {
			tstring h = *it;
			HttpAddRequestHeaders(hReq, h.c_str(), (DWORD)h.size(), 
								  HTTP_ADDREQ_FLAG_REPLACE | HTTP_ADDREQ_FLAG_ADD);
			it++;
		}
	}

	// �ؖ����n�𖳎�����ꍇ�̐ݒ���s��
	if (secure && !checkCert) {
		DWORD dwError = 0;
		DWORD dwFlags;
		DWORD dwBuffLen = sizeof(dwFlags);
		BOOL ret;

		if ((ret = InternetQueryOption(hReq, INTERNET_OPTION_SECURITY_FLAGS,
									   (LPVOID)&dwFlags, &dwBuffLen)) == FALSE) {
			storeErrorMessage(GetLastError(), errorMessage);
			closeHandle();
			return false;
		}

		dwFlags |= SECURITY_FLAG_IGNORE_UNKNOWN_CA;
		dwFlags |= SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
		dwFlags |= SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
		// dwFlags |= SECURITY_FLAG_IGNORE_REDIRECT_TO_HTTP;
		// dwFlags |= SECURITY_FLAG_IGNORE_REDIRECT_TO_HTTPS;

		if ((ret = InternetSetOption(hReq, INTERNET_OPTION_SECURITY_FLAGS,
								(LPVOID)&dwFlags, sizeof(dwFlags))) == FALSE) {
			storeErrorMessage(GetLastError(), errorMessage);
			closeHandle();
			return false;
		}
	}

	// ���N�G�X�g���M
	BOOL canagain = true;
again:
	if (!HttpSendRequest(hReq, NULL, 0, (void*)data, dataSize)) {
		DWORD dwError = GetLastError();
		// HTTPS �̔F�،n�G���[
		if (secure && (dwError == ERROR_INTERNET_INVALID_CA ||
					   dwError == ERROR_INTERNET_SEC_CERT_DATE_INVALID ||
					   dwError == ERROR_INTERNET_SEC_CERT_CN_INVALID)) {
			if (checkCert) {
				if (InternetErrorDlg (GetDesktopWindow(),
									  hReq,
									  dwError,
									  FLAGS_ERROR_UI_FILTER_FOR_ERRORS |
									  FLAGS_ERROR_UI_FLAGS_GENERATE_DATA |
									  FLAGS_ERROR_UI_FLAGS_CHANGE_OPTIONS,
									  NULL) == ERROR_SUCCESS) {
					goto again;
				}
			} else if (canagain) {
				DWORD dwFlags;
				DWORD dwBuffLen = sizeof(dwFlags);
				if (InternetQueryOption(hReq, INTERNET_OPTION_SECURITY_FLAGS, (LPVOID)&dwFlags, &dwBuffLen)) {
					dwFlags |= SECURITY_FLAG_IGNORE_UNKNOWN_CA;
					dwFlags |= SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
					dwFlags |= SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
					if (InternetSetOption(hReq, INTERNET_OPTION_SECURITY_FLAGS, (LPVOID)&dwFlags, sizeof(dwFlags))) {
						canagain = false;
						goto again;
					}
				}
			}
		}
		
		storeErrorMessage(dwError, errorMessage);
		closeHandle();
		return false;
	}

	return true;
}

/**
 * ���X�|���X����
 */
int 
HttpConnection::response(DownloadCallback callback, void *context)
{
	if (!isValid()) {
		return ERROR_INET;
	}
	
	statusCode = 0;
	DWORD length = sizeof statusCode;
	HttpQueryInfo(hReq, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, (LPVOID)&statusCode, &length, NULL);

	statusText.resize(0);
	length = 0;
	if (!HttpQueryInfo(hReq, HTTP_QUERY_STATUS_TEXT, NULL, &length, NULL) &&
		GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
		TCHAR *buf = (TCHAR*)malloc(sizeof(*buf) * length);
		if (buf && HttpQueryInfo(hReq, HTTP_QUERY_STATUS_TEXT, buf, &length, NULL)) {
			statusText = tstring(buf, length);
		}
		free(buf);
	}

	contentLength = 0;
	length = sizeof contentLength;
	HttpQueryInfo(hReq, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, 
				  (LPVOID)&contentLength,  &length, 0);
	
	contentType.resize(0);
	encoding.resize(0);
	length = 0;
	if (!HttpQueryInfo(hReq, HTTP_QUERY_CONTENT_TYPE, NULL, &length, NULL) &&
		GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
		TCHAR *buf = (TCHAR*)malloc(sizeof(*buf) * length);
		if (buf && HttpQueryInfo(hReq, HTTP_QUERY_CONTENT_TYPE, buf, &length, NULL)) {
			parseContentType(buf, length, contentType, encoding);
		}
		free(buf);
	}
	// HTML���p�[�X���� Content-Type ����G���R�[�f�B���O���擾����K�v������
	bool needParseHtml = _tcsicmp(contentType.c_str(), _T("text/html")) == 0 && encoding.size() == 0;
	
	// �S�w�b�_����͂��Ď擾
	responseHeaders.clear();
	length = 0;
	if (!HttpQueryInfo(hReq, HTTP_QUERY_RAW_HEADERS, NULL, &length, NULL) &&
		GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
		TCHAR *buf = (TCHAR*)malloc(sizeof(*buf) * length);
		if (buf && HttpQueryInfo(hReq, HTTP_QUERY_RAW_HEADERS, buf, &length, NULL) && length > 0) {
			size_t n = 0;
			while (n<length) {
				TCHAR *p = buf + n;
				int len = 0;
				while (n+len<length && p[len]) {
					len++;
				}
				if (len > 0) {
					int n2 = 0;
					while (n2 < len) {
						if (p[n2] == ':') {
							tstring name(p,n2++);
							tstring value(p+n2,len-n2);
							responseHeaders[name] = value;
							break;
						}
						n2++;
					}
				}
				n += (len+1);
			}
		}
		free(buf);
	}

	// HTTP �� OK ��Ԃ����ꍇ�̂݃t�@�C���Z�[�u�� enable �ɂ���
	if (statusCode == HTTP_STATUS_OK && callback) {
		DWORD size = 0;
		DWORD len;
		BYTE work[BUFSIZE];
		while (InternetReadFile(hReq, (void*)work, sizeof work, &len) && len > 0) {
			size += len;
			int percent = (contentLength > 0) ? size * 100 / contentLength : 0;
			// �擾�����t�@�C�������� META�^�O���Q�Ƃ��� Content-Type ���Ď擾
			if (needParseHtml) {
				tstring ctype;
#ifdef _UNICODE
				TCHAR *buf = new TCHAR[len];
				for (DWORD i=0;i<len;i++) {
					buf[i] = work[i];
				}
				tstring text(buf,len);
				delete[] buf;
#else
				tstring text(work,len);
#endif
				if (matchContentType(text, ctype)) {
					parseContentType(ctype.c_str(), ctype.size(), contentType, encoding);
				}
				needParseHtml = false;
			}
			if (!callback(context, work, len, percent)) {
				closeHandle();
				return ERROR_CANCEL;
			}
		}
		callback(context, NULL, 0, 100);
	}
	closeHandle();
	return ERROR_NONE;
}
