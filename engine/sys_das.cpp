#include "sys_das.h"

#include "vstdlib/jobthread.h"
#include "tier1/functors.h"
#include "filesystem.h"
#include "filesystem_engine.h"

#include <daScript/misc/sysos.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static CDaScriptSystem g_DaScriptSystem;
CDaScriptSystem* g_pDaScriptSystem = &g_DaScriptSystem;

static void CollectDasFiles( const char* subDir, std::vector< DasFile >& results )
{
	char basePath[MAX_PATH];

	if ( !g_pFileSystem )
	{
		return;
	}

	if ( !g_pFileSystem->GetLocalPath( subDir, basePath, sizeof( basePath ) ) )
	{
		return;
	}

	for ( const auto& entry : std::filesystem::recursive_directory_iterator( basePath ) )
	{
		if ( entry.is_regular_file() && entry.path().extension() == ".das" )
		{
			DasFile dasFile;
			dasFile.path			= entry.path().string();
			dasFile.last_write_time = entry.last_write_time();
			results.push_back( dasFile );
		}
	}
}

void CDaScriptSystem::Init()
{
	m_bShouldExit = false;
	g_pThreadPool->AddJob( new CFunctorJob( CreateFunctor( Job ) ) );
}

bool CDaScriptSystem::LoadFile( const DasFile& dasFile )
{
	DevMsg( "[daScript] Compiling %s ...\n", dasFile.path.c_str() );

	// See if the script compiles first
	das::TextPrinter tout;
	auto program = das::compileDaScript( dasFile.path, das::make_smart< das::FsFileAccess >(), tout, m_dasLibGroup );

	if ( program->failed() )
	{
		for ( auto& err : program->errors )
		{
			tout << das::reportError( err.at, err.what, err.extra, err.fixme, err.cerr );
		}

		DevMsg( "[daScript] Failed to load %s !\nErrors:\n%s\nEnd of errors.\n\n", dasFile.path.c_str(), tout.data() );
		return false;
	}

	DevMsg( "[daScript] Simulating %s ... \n", dasFile.path.c_str() );

	// Check if we can simulate it:
	// A Context is the runtime environment: stack, globals, heap.
	// simulate() resolves function pointers, initializes globals,
	// and prepares everything for execution. (according to daslang devs)

	auto ctx = std::make_shared< das::Context >( program->getContextStackSize() );

	if ( !program->simulate( *ctx, tout ) )
	{
		for ( auto& err : program->errors )
		{
			tout << das::reportError( err.at, err.what, err.extra, err.fixme, err.cerr );
		}

		DevMsg( "[daScript] Failed to simulate %s !\nErrors:\n%s\nEnd of errors.\n\n",
				dasFile.path.c_str(),
				tout.data() );

		return false;
	}

	DevMsg( "[daScript] Finding init/shutdown functions for %s ...\n", dasFile.path.c_str() );

	// Let's try initialize it by finding first if they have init and shutdown function
	auto fnInit = ctx->findFunction( "init" );

	if ( !fnInit )
	{
		DevMsg( "[daScript] Failed to find init function for script %s !\n", dasFile.path.c_str() );
		return false;
	}

	if ( !das::verifyCall< void >( fnInit->debugInfo, m_dasLibGroup ) )
	{
		DevMsg( "[daScript] Init signature failed for script %s !\n", dasFile.path.c_str() );
		return false;
	}

	auto fnShutdown = ctx->findFunction( "shutdown" );

	if ( !fnShutdown )
	{
		DevMsg( "[daScript] Failed to find shutdown function for script %s !\n", dasFile.path.c_str() );
		return false;
	}

	if ( !das::verifyCall< void >( fnInit->debugInfo, m_dasLibGroup ) )
	{
		DevMsg( "[daScript] Shutdown signature failed for script %s !\n", dasFile.path.c_str() );
		return false;
	}

	ctx->evalWithCatch( fnInit );

	if ( auto ex = ctx->getException() )
	{
		DevMsg( "[daScript] script %s returned exception ! (%s)\n", ex );
		return false;
	}

	m_AllProgramsLoaded.insert( dasFile.path );
	m_Programs.insert_or_assign( dasFile.path, DaScript { program, dasFile.last_write_time, fnInit, fnShutdown, ctx } );

	return true;
}

void CDaScriptSystem::LoadOrReloadFile( const DasFile& dasFile )
{
	auto dasProgram = m_Programs.find( dasFile.path );

	if ( dasProgram != m_Programs.end() )
	{
		if ( dasProgram->second.last_write_time == dasFile.last_write_time )
		{
			DevMsg( "[daScript] %s not changed, skipping.\n", dasFile.path.c_str() );
			return;
		}

		DevMsg( "[daScript] Reloading %s ...\n", dasFile.path.c_str() );

		m_Programs.erase( dasProgram );

		if ( !LoadFile( dasFile ) )
		{
			return;
		}

		DevMsg( "[daScript] Reloaded %s !\n", dasFile.path.c_str() );
	}
	else
	{
		DevMsg( "[daScript] Loading %s ...\n", dasFile.path.c_str() );

		if ( !LoadFile( dasFile ) )
		{
			return;
		}

		DevMsg( "[daScript] Loaded %s !\n", dasFile.path.c_str() );
	}
}

void CDaScriptSystem::Job()
{
	NEED_ALL_DEFAULT_MODULES;
	das::Module::Initialize();

	char dascriptsPath[MAX_PATH];
	if ( g_pFileSystem && g_pFileSystem->GetLocalPath( "dascripts", dascriptsPath, sizeof( dascriptsPath ) ) )
	{
		das::setDasRoot( dascriptsPath );
	}

	Msg( "[daScript] CDaScriptSystem initialized !\n" );

	while ( !g_pDaScriptSystem->m_bShouldExit )
	{
		std::vector< DasFile > dasFiles;
		CollectDasFiles( "dascripts/gamescripts", dasFiles );

		for ( auto&& dasFile : dasFiles )
		{
			g_pDaScriptSystem->LoadOrReloadFile( dasFile );
		}

		ThreadSleep( 500 );
	}

	// Call shutdown on all scripts & unload all of them
	das::Module::Shutdown();
}

void CDaScriptSystem::Shutdown()
{
	m_bShouldExit = true;
}