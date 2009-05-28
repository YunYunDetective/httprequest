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
		   output(NULL), outputLength(0), input(NULL), inputLength(0),
		   readyState(READYSTATE_UNINITIALIZED), statusCode(0)
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
	static tjs_error send(tTJSVariant *result, tjs_int numparams, tTJSVariant **params, HttpRequest *self) {
		self->checkRunning();
		self->checkOpen();
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
		//IStream *out = TVPCreateIStream(ttstr(saveFileName), TJS_BS_WRITE);
		self->startThread();
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
	 * @param upload ���M��
	 * @param percent �i��
	 */
	void onProgress(bool upload, int percent) {
		tTJSVariant params[2];
		params[0] = upload;
		params[1] = percent;
		static ttstr eventName(TJS_W("onProgress"));
		TVPPostEvent(objthis, objthis, eventName, 0, TVP_EPT_POST, 2, params);
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
				int lparam = (int)Message->LParam;
				self->onProgress((lparam & 0xff00)!=0, (lparam & 0xff));
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

	/**
	 * �t�@�C�����M����
	 * @param buffer �ǂݎ��o�b�t�@
	 * @param size �ǂݏo�����T�C�Y
	 */
	bool upload(void *buffer, DWORD &size) {
		if (input) {
			input->Read(buffer, size, &size);
		} else {
			size = 0;
		}
		inputSize += size;
		int percent = (inputLength > 0) ? inputSize * 100 / inputLength : 0;
		postMessage(WM_HTTP_PROGRESS, (WPARAM)this, 0x0100 | percent);
		return !canceled;
	}

	/**
	 * �ʐM���̃R�[���o�b�N����
	 * @return �L�����Z���Ȃ� false
	 */
	static bool uploadCallback(void *context, void *buffer, DWORD &size) {
		HttpRequest *self = (HttpRequest*)context;
		return self ? self->upload(buffer, size) : false;
	}
	
	/**
	 * �t�@�C���ǂݎ�菈��
	 * @param buffer �ǂݎ��o�b�t�@
	 * @param size �ǂݏo�����T�C�Y
	 */
	bool download(const void *buffer, DWORD size) {
		if (output) {
			if (buffer) {
				output->Write(buffer, size, &size);
			} else {
				output->Release();
			}
		}
		outputSize += size;
		int percent = (outputLength > 0) ? outputSize * 100 / outputLength : 0;
		postMessage(WM_HTTP_PROGRESS, (WPARAM)this, percent);
		return !canceled;
	}
	
	/**
	 * �ʐM���̃R�[���o�b�N����
	 * @return �L�����Z���Ȃ� false
	 */
	static bool downloadCallback(void *context, const void *buffer, DWORD size) {
		HttpRequest *self = (HttpRequest*)context;
		return self ? self->download(buffer, size) : false;
	}

	/**
	 * �o�b�N�O���E���h�Ŏ��s���鏈��
	 */
	void threadMain() {
		postMessage(WM_HTTP_READYSTATE, (WPARAM)this, (LPARAM)READYSTATE_SENT);
		inputSize = 0;
		int errorCode;
		if ((errorCode = http.request(uploadCallback, (void*)this)) == HttpConnection::ERROR_NONE) {
			http.queryInfo();
			outputSize = 0;
			outputLength = http.getContentLength();
			postMessage(WM_HTTP_READYSTATE, (WPARAM)this, (LPARAM)READYSTATE_RECEIVING);
			errorCode = http.response(downloadCallback, (void*)this);
		}
		switch (errorCode) {
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

	// ���N�G�X�g
	IStream *input;
	int inputLength;
	int inputSize;

	// ���X�|���X
	IStream *output;
	int outputLength;
	int outputSize;

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
