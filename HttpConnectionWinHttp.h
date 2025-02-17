#ifndef __HTTPCONNECTION_WINHTTP_H_
#define __HTTPCONNECTION_WINHTTP_H_

#include "HttpConnection.h"
#include <winhttp.h>


/**
 * HTTP接続を実現するクラス
 */
class HttpConnectionWinHttp : public HttpConnection
{
public:
	/**
	 * コンストラクタ
	 * @param agentName エージェント名
	 * @param checkCert 認証確認するかどうか
	 */
	HttpConnectionWinHttp(tstring agentName, bool checkCert=false)
		: HttpConnection(agentName, checkCert) {
		hSession = nullptr;
		hConnect = nullptr;
		hRequest = nullptr;
	}

	virtual ~HttpConnectionWinHttp() {
		closeHandle();
	}

	// ハンドルをクリア
	virtual void closeHandle();

	virtual bool open(const TCHAR *method,
					  const TCHAR *url,
					  const TCHAR *user = NULL,
					  const TCHAR *passwd = NULL);

	virtual int request(RequestCallback requestCallback=NULL,
						RetryCallback retryCallback=NULL,
						void *context=NULL);

	virtual void queryInfo();

	virtual int response(ResponseCallback callback=NULL, void *context=NULL);

	virtual bool isValid() const { return (hRequest != NULL); }

private:
	// WinHttp 用ハンドル
	HINTERNET hSession; ///< セッション(WinHttpOpen)
	HINTERNET hConnect; ///< 接続(WinHttpConnect)
	HINTERNET hRequest; ///< リクエスト(WinHttpOpenRequest)
};

#endif	// __HTTPCONNECTION_WINHTTP_H_#
