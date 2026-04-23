#ifndef SYS_DAS_H
#define SYS_DAS_H

#ifdef _WIN32
#pragma once
#endif

#include <filesystem>
#include <unordered_map>
#include <unordered_set>

#include "daScript/daScript.h"

struct DasFile
{
	std::string path;
	std::filesystem::file_time_type last_write_time;
};

class CDaScriptSystem
{
  public:
	void Init();
	void Shutdown();
	bool LoadFile( const DasFile& dasFile );
	void LoadOrReloadFile( const DasFile& dasFile );
	static void Job();

	bool m_bShouldExit;

	struct DaScript
	{
		das::ProgramPtr program;
		std::filesystem::file_time_type last_write_time;
	};

	std::unordered_map< std::string, DaScript > m_Programs;
	std::unordered_set< std::string > m_AllProgramsLoaded;
	das::ModuleGroup m_dasLibGroup;
};

extern CDaScriptSystem* g_pDaScriptSystem;

#endif
