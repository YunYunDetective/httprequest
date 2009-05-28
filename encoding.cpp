#include <windows.h>
#include <comdef.h>
#include <mlang.h>

static bool coInitialized;
static IMultiLanguage *ml = NULL;

void
initCodePage()
{
	coInitialized = SUCCEEDED(CoInitialize(0));
	if (coInitialized) {
		::CoCreateInstance(CLSID_CMultiLanguage, NULL, CLSCTX_ALL, IID_IMultiLanguage, (LPVOID*)&ml);
	}
}

void
doneCodePage()
{
	if (coInitialized) {
		if (ml) {
			ml->Release();
			ml = NULL;
		}
		CoUninitialize();
	}
}

/**
 * IANA �̃G���R�[�h������ codepage ���擾
 * @param encoding �G���R�[�h
 * @return �R�[�h�y�[�W
 */
int
getCodePage(const wchar_t *encoding)
{
	if (encoding && *encoding && ml) {
		MIMECSETINFO csinfo;
		if (SUCCEEDED(ml->GetCharsetInfo(_bstr_t(encoding), &csinfo))) {
			return csinfo.uiInternetEncoding;
		}
	}
	return CP_UTF8;
}
