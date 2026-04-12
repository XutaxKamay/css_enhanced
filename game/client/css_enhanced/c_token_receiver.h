#ifndef CSS_ENHANCED_TOKEN_RECEIVER_H
#define CSS_ENHANCED_TOKEN_RECEIVER_H

#ifdef _WIN32
#pragma once
#endif

#include "tier0/platform.h"

class CTokenReceiver
{
public:
	CTokenReceiver();
	~CTokenReceiver();

	void Start();
	void Stop();

	// Call from main thread to apply any pending token
	void ProcessPendingToken();

private:
	void RunServer();
	static uintp ServerThreadProc( void *pParam );

	int m_nPort;
	volatile bool m_bRunning;
	int m_iSocket;
};

extern CTokenReceiver g_TokenReceiver;

#endif
