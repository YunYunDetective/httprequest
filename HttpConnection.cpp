#include "HttpConnection.h"

/**
 * Content-Type をパースして ContentType と encoding 指定を取得
 * @param buf バッファ
 * @param length バッファサイズ
 * @param contentType 取得した Content-Type を格納
 * @param encoding 取得したエンコード指定を格納
 */
void
HttpConnection::parseContentType(const TCHAR *buf, size_t length, tstring &contentType, tstring &encoding)
{
	// 頭のスペースを読み飛ばす
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
	} else if (_tcsicmp(name, _T("Content-Length")) == 0) {
		requestContentLength = _tcstol(value, NULL, 10);
	}
	tstring n = name;
	n += _T(":");
	n += value;
	header.push_back(n);
}
