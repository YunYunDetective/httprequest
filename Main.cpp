#include "HttpConnection.h"
#include "ncbind/ncbind.hpp"
#include <vector>
using namespace std;
#include <process.h>

// ���b�Z�[�W�R�[�h
#define	WM_HTTP_READYSTATE	(WM_APP+6)	// �X�e�[�g�ύX
#define	WM_HTTP_PROGRESS	(WM_APP+7)	// �v���O���X���

// �G�[�W�F���g��
#define AGENT_NAME			_T("KIRIKIRI")

// Content-Type �擾�p RegExp �C���X�^���X
static tTJSVariant *regctype = NULL;

bool
matchContentType(const BYTE *text, tstring &ctype)
{
	bool ret = false;
	
	tTJSVariant t(ttstr((const tjs_nchar*)text));
	tTJSVariant *params[] = { &t };
	tTJSVariant matchResult;
	if (TJS_SUCCEEDED(regctype->AsObjectClosureNoAddRef().FuncCall(0, TJS_W("match"), NULL, &matchResult, 1, params, NULL))) {
		tTJSVariant matchStr;
		if (TJS_SUCCEEDED(matchResult.AsObjectClosureNoAddRef().PropGetByNum(0, 2, &matchStr, NULL))) {
			tTJSVariantString *str = matchStr.AsString();
			if (str) {
				int len = str->GetLength();
				const tjs_char *buf = *str;
				if (len > 0) {
					if (buf[0] == '\'' || buf[0] == '"') {
						ctype = tstring(buf+1, len-2);
					} else {
						ctype = tstring(buf, len);
					}
					ret = true;
				}
				str->Release();
			}
		}
	}
	return ret;
}

/**
 * HttpRequest �N���X
 */
class HttpRequest {

public:

	enum ReadyState {
		READYSTATE_UNINITIALIZED,
		READYSTATE_OPEN,
		READYSTATE_SENT,
		READYSTATE_RECEIVING,
		READYSTATE_LOADED
	};

	/**
	 * �R���X�g���N�^
	 * @param objthis ���ȃI�u�W�F�N�g
	 * @param window �e�E�C���h�E
	 * @param cert HTTP�ʐM���ɏؖ����`�F�b�N���s��
	 */
	HttpRequest(iTJSDispatch2 *objthis, iTJSDispatch2 *window, bool cert, const tjs_char *agentName)
		 : objthis(objthis), window(window), http(agentName, cert),
		   threadHandle(NULL), canceled(false),
		   output(NULL), readyState(READYSTATE_UNINITIALIZED), statusCode(0)
	{
		window->AddRef();
		setReceiver(true);
	}
	
	/**
	 * �f�X�g���N�^
	 */
	~HttpRequest() {
		abort();
		setReceiver(false);
		window->Release();
	}

	/**
	 * �w�肵�����\�b�h�Ŏw��URL�Ƀ��N�G�X�g����
	 * ����ɔ񓯊��ł̌Ăяo���ɂȂ�܂�
	 * @param method GET|PUT|POST �̂����ꂩ
	 * @param url ���N�G�X�g���URL
	 * @param userName ���[�U���B�w�肷��ƔF�؃w�b�_�����܂�
	 * @param password �p�X���[�h
	 */
	void _open(const tjs_char *method, const tjs_char *url, const tjs_char *userName, const tjs_char *password) {
		abort();
		if (http.open(method, url, userName, password)) {
			onReadyStateChange(READYSTATE_OPEN);
		} else {
			TVPThrowExceptionMessage(http.getErrorMessage());
		}
	}

	static tjs_error open(tTJSVariant *result, tjs_int numparams, tTJSVariant **params, HttpRequest *self) {
		if (numparams < 2) {
			return TJS_E_BADPARAMCOUNT;
		}
		self->_open(params[0]->GetString(), params[1]->GetString(), numparams > 2 ? params[2]->GetString() : NULL, numparams > 3 ? params[3]->GetString() : NULL);
		return TJS_S_OK;
	}
	
	/**
	 * ���M���ɑ�����w�b�_�[��ǉ�����
	 * @param name �w�b�_��
	 * @param value �l
	 */
	void setRequestHeader(const tjs_char *name, const tjs_char *value) {
		checkRunning();
		http.addHeader(name, value);
	}

	/**
	 * ���N�G�X�g�̑��M
	 * @param data ���M����f�[�^
	 * ������̏ꍇ�F���̂܂ܑ��M
	 * �����̏ꍇ: application/x-www-form-urlencoded �ő��M
	 */
	void _send(const void *data, int size) {
		checkRunning();
		checkOpen();
		if (data) {
		}
		startThread();
	}

	static tjs_error send(tTJSVariant *result, tjs_int numparams, tTJSVariant **params, HttpRequest *self) {
		void *data = NULL;
		int size = 0;
		if (numparams > 0) {
			switch (params[0]->Type()) {
			case tvtVoid:
				break;
			case tvtObject:
				break;
			case tvtString:
				// UTF-8 �ɕϊ�
				break;
			case tvtOctet:
				break;
			default:
				break;
			}
		}
		self->_send(data, size);
		return TJS_S_OK;
	}

	/**
	 * ���ݎ��s���̑���M�̃L�����Z��
	 */
	void abort() {
		stopThread();
	}
	
	/**
	 * ���ׂĂ� HTTP�w�b�_���擾����
	 * @return HTTP�w�b�_���i�[���ꂽ����
	 */
	tTJSVariant getAllResponseHeaders() {
		iTJSDispatch2 *dict = TJSCreateDictionaryObject();
		tstring name;
		tstring value;
		http.initRH();
		while (http.getNextRH(name, value)) {
			tTJSVariant v(value.c_str());
			dict->PropSet(TJS_MEMBERENSURE, name.c_str(), NULL, &v, dict);
		}
		return tTJSVariant(dict,dict);
	}

	/**
	 * �w�肵��HTTP�w�b�_���擾����
	 * @param name �w�b�_���x����
	 * @return �w�b�_�̒l
	 */
	const tjs_char *getResponseHeader(const tjs_char *name) {
		return http.getResponseHeader(name);
	}

	/**
	 * �ʐM��ԁB�ǂݍ��ݐ�p
	 * @return ���݂̒ʐM���
	 * 0: �������
	 * 1: �ǂݍ��ݒ�
	 * 2: �ǂݍ���
	 * 3: ��͒�
	 * 4: ����
	 */
	int getReadyState() const {
		return readyState;
	}

	/**
	 * ���X�|���X�f�[�^�B�ǂݍ��ݐ�p
	 * @return ���X�|���X�f�[�^
	 */
	tTJSVariant getResponse() {
		return tTJSVariant();
	}
		
	/**
	 * ���X�|���X�� HTTP�X�e�[�^�X�R�[�h�B�ǂݍ��ݐ�p
	 * @return �X�e�[�^�X�R�[�h
	 */
	int getStatus() {
		return statusCode;
	}
	
	/**
	 * ���X�|���X�� HTTP�X�e�[�^�X�̕�����
	 * @return ���X�|���X������
	 */
	const tjs_char *getStatusText() {
		return statusText.c_str();
	}

	/**
	 * �C���X�^���X�����t�@�N�g��
	 */
	static tjs_error factory(HttpRequest **result, tjs_int numparams, tTJSVariant **params, iTJSDispatch2 *objthis) {
		if (numparams < 1) {
			return TJS_E_BADPARAMCOUNT;
		}
		iTJSDispatch2 *window = params[0]->AsObjectNoAddRef();
		if (window->IsInstanceOf(0, NULL, NULL, L"Window", window) != TJS_S_TRUE) {
			TVPThrowExceptionMessage(L"InvalidObject");
		}
		*result = new HttpRequest(objthis, window, numparams >= 2 ? params[1]->AsInteger() != 0 : true, AGENT_NAME);
		return S_OK;
	}
	

protected:

	void checkRunning() {
		if (threadHandle) {
			TVPThrowExceptionMessage(TJS_W("already running"));
		}
	}

	void checkOpen() {
		if (!http.isValid()) {
			TVPThrowExceptionMessage(TJS_W("not open"));
		}
	}

	/**
	 * readyState ���ω������ꍇ�̃C�x���g����
	 * @param readyState �V�����X�e�[�g
	 * @return ���f����ꍇ�� true ��Ԃ�
	 */
	void onReadyStateChange(int readyState) {
		this->readyState = readyState;
		if (readyState == READYSTATE_LOADED) {
			stopThread();
		}
		tTJSVariant param(readyState);
		static ttstr eventName(TJS_W("onReadyStateChange"));
		TVPPostEvent(objthis, objthis, eventName, 0, TVP_EPT_POST, 1, &param);
	}
	
	/**
	 * �f�[�^�ǂݍ��ݒ��̃C�x���g����
	 * @param percent �i��
	 * @return ���f����ꍇ�� true ��Ԃ�
	 */
	void onProgress(int percent) {
		tTJSVariant param(percent);
		static ttstr eventName(TJS_W("onProgress"));
		TVPPostEvent(objthis, objthis, eventName, 0, TVP_EPT_POST, 1, &param);
	}

	
	// ���[�U���b�Z�[�W���V�[�o�̓o�^/����
	void setReceiver(bool enable) {
		tTJSVariant mode     = enable ? (tTVInteger)(tjs_int)wrmRegister : (tTVInteger)(tjs_int)wrmUnregister;
		tTJSVariant proc     = (tTVInteger)(tjs_int)receiver;
		tTJSVariant userdata = (tTVInteger)(tjs_int)this;
		tTJSVariant *p[] = {&mode, &proc, &userdata};
		if (window->FuncCall(0, L"registerMessageReceiver", NULL, NULL, 4, p, objthis) != TJS_S_OK) {
			TVPThrowExceptionMessage(L"can't regist user message receiver");
		}
	}

	/**
	 * �C�x���g��M����
	 */
	static bool __stdcall receiver(void *userdata, tTVPWindowMessage *Message) {
		HttpRequest *self = (HttpRequest*)userdata;
		switch (Message->Msg) {
		case WM_HTTP_READYSTATE:
			if (self == (HttpRequest*)Message->WParam) {
				self->onReadyStateChange((ReadyState)Message->LParam);
				return true;
			}
			break;
		case WM_HTTP_PROGRESS:
			if (self == (HttpRequest*)Message->WParam) {
				self->onProgress((int)Message->LParam);
				return true;
			}
			break;
		}
		return false;
	}

	// -----------------------------------------------
	// �X���b�h����
	// -----------------------------------------------

	// ���b�Z�[�W���M
	void postMessage(UINT msg, WPARAM wparam=NULL, LPARAM lparam=NULL) {
		// �E�B���h�E�n���h�����擾���Ēʒm
		tTJSVariant val;
		window->PropGet(0, TJS_W("HWND"), NULL, &val, objthis);
		HWND hwnd = reinterpret_cast<HWND>((tjs_int)(val));
		::PostMessage(hwnd, msg, wparam, lparam);
	}

	bool download(void *buffer, DWORD size, int percent) {
		if (buffer) {
			if (output) {
				output->Write(buffer, size, &size);
			}
		} else {
			if (output) {
				output->Release();
			}
		}
		postMessage(WM_HTTP_PROGRESS, (WPARAM)this, percent);
		return !canceled;
	}
	
	/**
	 * �ʐM���̃R�[���o�b�N����
	 * @return �L�����Z���Ȃ� false
	 */
	static bool downloadCallback(void *context, void *buffer, DWORD size, int percent) {
		HttpRequest *self = (HttpRequest*)context;
		return self ? self->download(buffer, size, percent) : false;
	}
	
	void threadMain() {
		postMessage(WM_HTTP_READYSTATE, (WPARAM)this, (LPARAM)READYSTATE_SENT);
		if (!http.request()) {
			statusCode = 0;
			statusText = http.getErrorMessage();
		} else {
			postMessage(WM_HTTP_READYSTATE, (WPARAM)this, (LPARAM)READYSTATE_RECEIVING);
			//IStream *out = TVPCreateIStream(ttstr(saveFileName), TJS_BS_WRITE);
			

			switch (http.response(downloadCallback, (void*)this)) {
			case HttpConnection::ERROR_NONE:
				statusCode = http.getStatusCode();
				statusText = http.getStatusText();
				break;
			case HttpConnection::ERROR_CANCEL:
				statusCode = -1;
				statusText = L"aborted";
				break;
			default:
				statusCode = 0;
				statusText = http.getErrorMessage();
				break;
			}
		}
		postMessage(WM_HTTP_READYSTATE, (WPARAM)this, (LPARAM)READYSTATE_LOADED);
	}

	// ���s�X���b�h
	static unsigned __stdcall threadFunc(void *data) {
		((HttpRequest*)data)->threadMain();
		_endthreadex(0);
		return 0;
	}

	// �X���b�h�����J�n
	void startThread() {
		stopThread();
		canceled = false;
		threadHandle = (HANDLE)_beginthreadex(NULL, 0, threadFunc, this, 0, NULL);
	}

	// �X���b�h�����I��
	void stopThread() {
		if (threadHandle) {
			canceled = true;
			WaitForSingleObject(threadHandle, INFINITE);
			CloseHandle(threadHandle);
			threadHandle = 0;
		}
	}
	
private:
	iTJSDispatch2 *objthis; ///< �I�u�W�F�N�g���̎Q��
	iTJSDispatch2 *window; ///< �I�u�W�F�N�g���̎Q��

	// �ʐM�p����
	HttpConnection http;

	// �����p
	HANDLE threadHandle; ///< �X���b�h�̃n���h��
	bool canceled; ///< �L�����Z�����ꂽ

	// ����
	IStream *output;
	int readyState;
	int statusCode;
	ttstr statusText;
};

#define ENUM(n) Variant(#n, (int)HttpRequest::READYSTATE_ ## n)

NCB_REGISTER_CLASS(HttpRequest) {
	Factory(&ClassT::factory);
	ENUM(UNINITIALIZED);
	ENUM(OPEN);
	ENUM(SENT);
	ENUM(RECEIVING);
	ENUM(LOADED);
	RawCallback(TJS_W("open"), &Class::open, 0);
	NCB_METHOD(setRequestHeader);
	RawCallback(TJS_W("send"), &Class::send, 0);
	NCB_METHOD(abort);
	NCB_METHOD(getAllResponseHeaders);
	NCB_METHOD(getResponseHeader);
	NCB_PROPERTY_RO(readyState, getReadyState);
	NCB_PROPERTY_RO(response, getResponse);
	NCB_PROPERTY_RO(status, getStatus);
	NCB_PROPERTY_RO(statusText, getStatusText);
}

static void
PreRegistCallback()
{
	// Content-Type �擾�p�̐��K�\���I�u�W�F�N�g���擾
	regctype = new tTJSVariant();
	TVPExecuteExpression(TJS_W("new RegExp(\"<meta[ \\t]+http-equiv=(\\\"content-type\\\"|'content-type'|content-type)[ \\t]+content=(\\\"[^\\\"]*\\\"|'[^']*'|[^ \\t>]+).*>\",\"i\")"), regctype);
}

static void
PostUnregistCallback()
{
	// Content-Type �擾�p�̐��K�\���I�u�W�F�N�g�����
	delete regctype;
}

NCB_PRE_REGIST_CALLBACK(PreRegistCallback);
NCB_POST_UNREGIST_CALLBACK(PostUnregistCallback);
