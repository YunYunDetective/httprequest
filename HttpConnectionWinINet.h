#ifndef __HTTPCONNECTION_WININET_H_
#define __HTTPCONNECTION_WININET_H_

#include "HttpConnection.h"
#include <WinInet.h>


/**
 * HTTP接続を実現するクラス
 */
class HttpConnectionWinINet : public HttpConnection
{
public:
	/**
	 * コンストラクタ
	 * @param agentName エージェント名
	 * @param checkCert 認証確認するかどうか
	 */
	HttpConnectionWinINet(tstring agentName, bool checkCert=false)
		: HttpConnection(agentName, checkCert) {
		hInet = NULL;
		hConn = NULL;
		hReq  = NULL;
	}

	// デストラクタ
	virtual ~HttpConnectionWinINet(void) {
		closeHandle();
	}

	// ハンドルをクリア
	virtual void closeHandle();

	virtual bool open(const TCHAR *method,
					  const TCHAR *url,
					  const TCHAR *user = NULL,
					  const TCHAR *passwd = NULL);

	virtual int request(RequestCallback requestCallback=NULL, RetryCallback retryCalblack = NULL, void *context=NULL);

	virtual void queryInfo();

	virtual int response(ResponseCallback callback=NULL, void *context=NULL);

	virtual bool isValid() const { return (hReq != NULL); }

private:
	HINTERNET hInet; ///< インターネット接続
	HINTERNET hConn; ///< コネクション
	HINTERNET hReq;  ///< HTTPリクエスト
};

#endif	// __HTTPCONNECTION_WININET_H_#
