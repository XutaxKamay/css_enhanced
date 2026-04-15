#ifndef CSS_ENHANCED_TOKEN_RECEIVER_H
#define CSS_ENHANCED_TOKEN_RECEIVER_H

#ifdef _WIN32
#pragma once
#endif

#include "tier0/platform.h"
#include "tier0/threadtools.h"

class CTokenReceiver
{
  public:
	CTokenReceiver();
	~CTokenReceiver();

	void Start();
	void Stop();

  private:
	void RunServer();
	static uintp ServerThreadProc( void* pParam );

	int m_nPort;
	volatile bool m_bRunning;
	volatile bool m_bInitialized;
	int m_iSocket;
	ThreadHandle_t m_hThread;
	CThreadMutex m_Mutex;
};

extern CTokenReceiver g_TokenReceiver;

#endif