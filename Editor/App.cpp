#include "EditorPch.h"
#include "App.h"

#include "Platform/Process.h"
#include "Platform/Exception.h"
#include "Platform/Trace.h"
#include "Platform/Timer.h"
#include "Platform/Console.h"
#include "Platform/Timer.h"

#include "Foundation/Exception.h"
#include "Foundation/Log.h"
#include "Foundation/Math.h"
#include "Foundation/Name.h"

#include "Reflect/Registry.h"

#include "Application/Startup.h"
#include "Application/InitializerStack.h"
#include "Application/CmdLineProcessor.h"
#include "Application/DocumentManager.h"

#include "Engine/FileLocations.h"
#include "Engine/AsyncLoader.h"
#include "Engine/AssetLoader.h"
#include "Engine/CacheManager.h"
#include "Engine/Config.h"
#include "Engine/Asset.h"

#include "EngineJobs/EngineJobs.h"

#include "GraphicsJobs/GraphicsJobs.h"

#include "Framework/WorldManager.h"
#include "Framework/TaskScheduler.h"
#include "Framework/SystemDefinition.h"

#include "PcSupport/AssetPreprocessor.h"
#include "PcSupport/ConfigPc.h"
#include "PcSupport/LooseAssetLoader.h"
#include "PcSupport/PlatformPreprocessor.h"

#include "PreprocessingPc/PcPreprocessor.h"

#include "EditorSupport/EditorSupportPch.h"
#include "EditorSupport/FontResourceHandler.h"

#include "EditorScene/EditorSceneInit.h"
#include "EditorScene/SettingsManager.h"

#include "Editor/ArtProvider.h"
#include "Editor/Input.h"
#include "Editor/EditorGeneratedWrapper.h"
#include "Editor/Perforce/Perforce.h"
#include "Editor/ProjectViewModel.h"
#include "Editor/Settings/EditorSettings.h"
#include "Editor/Settings/WindowSettings.h"
#include "Editor/Perforce/Perforce.h"
#include "Editor/Dialogs/PerforceWaitDialog.h"
#include "Editor/Vault/VaultSettings.h"

#include "Editor/Commands/ProfileDumpCommand.h"

#include "Editor/Clipboard/ClipboardDataWrapper.h"
#include "Editor/Clipboard/ClipboardFileList.h"

#include "Editor/Inspect/Widgets/DrawerWidget.h"
#include "Editor/Inspect/Widgets/LabelWidget.h"
#include "Editor/Inspect/Widgets/ValueWidget.h"
#include "Editor/Inspect/Widgets/SliderWidget.h"
#include "Editor/Inspect/Widgets/ChoiceWidget.h"
#include "Editor/Inspect/Widgets/CheckBoxWidget.h"
#include "Editor/Inspect/Widgets/ColorPickerWidget.h"
#include "Editor/Inspect/Widgets/ListWidget.h"
#include "Editor/Inspect/Widgets/ButtonWidget.h"
#include "Editor/Inspect/Widgets/FileDialogButtonWidget.h"
#include "Editor/Inspect/TreeCanvas.h"
#include "Editor/Inspect/TreeCanvasWidget.h"
#include "Editor/Inspect/StripCanvas.h"
#include "Editor/Inspect/StripCanvasWidget.h"

// TODO: Support dynamic loading of game types in the editor
#include "Bullet/BulletPch.h"
#include "ExampleGame/ExampleGamePch.h"
#include "Components/ComponentsPch.h"

#include <wx/wx.h>
#include <wx/choicdlg.h>
#include <wx/cmdline.h>
#include <wx/splash.h>
#include <wx/cshelp.h>

#if HELIUM_OS_WIN
# include <wx/msw/private.h>
#endif

using namespace Helium;
using namespace Helium::Editor;
using namespace Helium::CommandLine;

bool g_HelpFlag = false;
bool g_DisableTracker = false;

namespace Helium
{
	namespace Editor
	{
		IMPLEMENT_APP( App );
	}
}

namespace
{
	AssetPath g_EditorSystemDefinitionPath( "/Editor:System" );
	SystemDefinitionPtr g_EditorSystemDefinition;
}

void InitializeEditorSystem()
{
	HELIUM_ASSERT( AssetLoader::GetStaticInstance() );
	AssetLoader::GetStaticInstance()->LoadObject<SystemDefinition>( g_EditorSystemDefinitionPath, g_EditorSystemDefinition );
	if ( !g_EditorSystemDefinition )
	{
		HELIUM_TRACE( TraceLevels::Error, TXT( "GameSystem::Initialize(): Could not find SystemDefinition. LoadObject on '%s' failed.\n" ), *g_EditorSystemDefinitionPath.ToString() );
	}
	else
	{
		g_EditorSystemDefinition->Initialize();
	}
}

void DestroyEditorSystem()
{
	// TODO: Figure out why loading g_EditorSystemDefinition randomly doesn't work
	//if ( HELIUM_VERIFY( g_EditorSystemDefinition ))
	if ( g_EditorSystemDefinition )
	{
		g_EditorSystemDefinition->Cleanup();
		g_EditorSystemDefinition = 0;
	}
}

#ifdef IDLE_LOOP
BEGIN_EVENT_TABLE( App, wxApp )
EVT_IDLE( App::OnIdle )
END_EVENT_TABLE()
#endif

App::App()
: m_Running( false )
, m_AppVersion( HELIUM_APP_VERSION )
, m_AppName( HELIUM_APP_NAME )
, m_AppVerName( HELIUM_APP_VER_NAME )
, m_SettingsManager( new SettingsManager() )
// TODO: This needs fixing otherwise dialogs will not be modal -geoff
, m_Frame( NULL )
{
}

App::~App()
{
}

///////////////////////////////////////////////////////////////////////////////
// Called after OnInitCmdLine.  The base class handles the /help command line
// switch and exits.  If we get this far, we need to parse the command line
// and determine what mode to launch the app in.
// 
bool App::OnInit()
{
	SetVendorName( HELIUM_APP_NAME );

#if !HELIUM_RELEASE && !HELIUM_PROFILE
	Helium::InitializeSymbols();
#endif

	// don't spend a lot of time updating idle events for windows that don't need it
	wxUpdateUIEvent::SetMode( wxUPDATE_UI_PROCESS_SPECIFIED );
	wxIdleEvent::SetMode( wxIDLE_PROCESS_SPECIFIED );

	Helium::FilePath exePath( GetProcessPath() );
	Helium::FilePath iconFolder( exePath.Directory() + TXT( "Icons/" ) );

	wxInitAllImageHandlers();
	wxImageHandler* curHandler = wxImage::FindHandler( wxBITMAP_TYPE_CUR );
	if ( curHandler )
	{
		// Force the cursor handler to the end of the list so that it doesn't try to
		// open TGA files.
		wxImage::RemoveHandler( curHandler->GetName() );
		curHandler = NULL;
		wxImage::AddHandler( new wxCURHandler );
	}

	ArtProvider* artProvider = new ArtProvider();
	wxArtProvider::Push( artProvider );

	wxSimpleHelpProvider* helpProvider = new wxSimpleHelpProvider();
	wxHelpProvider::Set( helpProvider );

	// Make sure various module-specific heaps are initialized from the main thread before use.
	InitEngineJobsDefaultHeap();
	InitGraphicsJobsDefaultHeap();

	// Register shutdown for general systems.
	m_InitializerStack.Push( FileLocations::Shutdown );
	m_InitializerStack.Push( Name::Shutdown );
	m_InitializerStack.Push( AssetPath::Shutdown );

	// Async I/O.
	AsyncLoader& asyncLoader = AsyncLoader::GetStaticInstance();
	HELIUM_VERIFY( asyncLoader.Initialize() );
	m_InitializerStack.Push( AsyncLoader::DestroyStaticInstance );

	// Asset cache management.
	FilePath baseDirectory;
	if ( !FileLocations::GetBaseDirectory( baseDirectory ) )
	{
		HELIUM_TRACE( TraceLevels::Error, TXT( "Could not get base directory." ) );
		return false;
	}

	HELIUM_VERIFY( CacheManager::InitializeStaticInstance( baseDirectory ) );
	m_InitializerStack.Push( CacheManager::DestroyStaticInstance );

	// libs
	Editor::PerforceWaitDialog::EnableWaitDialog( true );
	m_InitializerStack.Push( Perforce::Initialize, Perforce::Cleanup );
	m_InitializerStack.Push( Reflect::ObjectRefCountSupport::Shutdown );
	m_InitializerStack.Push( Asset::Shutdown );
	m_InitializerStack.Push( AssetType::Shutdown );
	m_InitializerStack.Push( Reflect::Initialize, Reflect::Cleanup );
	m_InitializerStack.Push( Editor::Initialize,  Editor::Cleanup );

	// Asset loader and preprocessor.
	HELIUM_VERIFY( LooseAssetLoader::InitializeStaticInstance() );
	m_InitializerStack.Push( LooseAssetLoader::DestroyStaticInstance );

	AssetLoader* pAssetLoader = AssetLoader::GetStaticInstance();
	HELIUM_ASSERT( pAssetLoader );

	AssetPreprocessor* pAssetPreprocessor = AssetPreprocessor::CreateStaticInstance();
	HELIUM_ASSERT( pAssetPreprocessor );
	PlatformPreprocessor* pPlatformPreprocessor = new PcPreprocessor;
	HELIUM_ASSERT( pPlatformPreprocessor );
	pAssetPreprocessor->SetPlatformPreprocessor( Cache::PLATFORM_PC, pPlatformPreprocessor );

	m_InitializerStack.Push( AssetPreprocessor::DestroyStaticInstance );
	m_InitializerStack.Push( ThreadSafeAssetTrackerListener::DestroyStaticInstance );
	m_InitializerStack.Push( AssetTracker::DestroyStaticInstance );

	m_InitializerStack.Push( InitializeEditorSystem, DestroyEditorSystem );

	//HELIUM_ASSERT( g_EditorSystemDefinition.Get() ); // TODO: Figure out why this sometimes doesn't load
	Helium::Components::Initialize( g_EditorSystemDefinition.Get() );
	m_InitializerStack.Push( Components::Cleanup );

	// Engine configuration.
	Config& rConfig = Config::GetStaticInstance();
	rConfig.BeginLoad();
	while( !rConfig.TryFinishLoad() )
	{
		pAssetLoader->Tick();
	}

	m_InitializerStack.Push( Config::DestroyStaticInstance );

	ConfigPc::SaveUserConfig();
	
	LoadSettings();

	Connect( wxEVT_CHAR, wxKeyEventHandler( App::OnChar ), NULL, this );

	m_Frame = new MainFrame( m_SettingsManager );

#if HELIUM_OS_WIN
	m_Engine.Initialize( &m_Frame->GetSceneManager(), GetHwndOf( m_Frame ) );
#else
	m_Engine.Initialize( &m_Frame->GetSceneManager(), NULL );
#endif

	HELIUM_VERIFY( m_Frame->Initialize() );
	m_Frame->Show();

	if ( GetSettingsManager()->GetSettings< EditorSettings >()->GetReopenLastProjectOnStartup() )
	{
		const std::vector< std::string >& mruPaths = wxGetApp().GetSettingsManager()->GetSettings<EditorSettings>()->GetMRUProjects();
		if ( !mruPaths.empty() )
		{
			FilePath projectPath( *mruPaths.rbegin() );
			if ( projectPath.Exists() )
			{
				m_Frame->OpenProject( FilePath( *mruPaths.rbegin() ) );
			}
		}
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////
// Run the main application message pump
// 
int App::OnRun()
{
	m_Running = true;

	return wxApp::OnRun();
}

///////////////////////////////////////////////////////////////////////////////
// Called when the application is being exited.  Cleans up resources.
// 
int App::OnExit()
{
	Disconnect( wxEVT_CHAR, wxKeyEventHandler( App::OnChar ), NULL, this );

	SaveSettings();

	m_Engine.Shutdown();

	m_SettingsManager.Release();

	m_InitializerStack.Cleanup();

	wxImage::CleanUpHandlers();

	int result = wxApp::OnExit();

	// Always clear out memory heaps last.
	ThreadLocalStackAllocator::ReleaseMemoryHeap();

	return result;
}

void App::OnChar( wxKeyEvent& event )
{
	// It seems like this is swallowing all events to all text fields.. disabling for now
	event.Skip();
	return;

#if KEYBOARD_REFACTOR
	if ( !m_Frame )
	{
		return;
	}

	Helium::KeyboardInput input;
	Helium::ConvertEvent( event, input );
	std::string error;

	if ( input.IsCtrlDown() )
	{
		switch( input.GetKeyCode() )
		{
		case KeyCodes::a: // ctrl-a
			{
				wxCommandEvent evt ( wxEVT_COMMAND_MENU_SELECTED, wxID_SELECTALL );
				m_Frame->GetEventHandler()->ProcessEvent( evt );
				event.Skip( false );
				return;
			}

		case KeyCodes::i: // ctrl-i
			{
				m_Frame->InvertSelection();
				event.Skip( false );
				return;
			}

		case KeyCodes::o: // ctrl-o
			{
				m_Frame->OpenProjectDialog();
				event.Skip( false );
				return;
			}

		case KeyCodes::s: // ctrl-s
			{
				if ( !m_Frame->SaveAll( error ) )
				{
					wxMessageBox( error.c_str(), wxT( "Error" ), wxCENTER | wxICON_ERROR | wxOK, m_Frame );
				}
				event.Skip( false );
				return;
			}

		case KeyCodes::v: // ctrl-v
			{
				wxCommandEvent evt ( wxEVT_COMMAND_MENU_SELECTED, wxID_PASTE );
				m_Frame->GetEventHandler()->ProcessEvent( evt );
				event.Skip( false );
				return;
			}

		case KeyCodes::w: // ctrl-w
			{
				m_Frame->CloseProject();
				event.Skip( false );
				return;
			}

		case KeyCodes::x: // ctrl-x
			{
				wxCommandEvent evt ( wxEVT_COMMAND_MENU_SELECTED, wxID_CUT );
				m_Frame->GetEventHandler()->ProcessEvent( evt );
				event.Skip( false );
				return;
			}

		default:
			break;
		}
	}
#endif
}

#ifdef IDLE_LOOP
void App::OnIdle( wxIdleEvent& event )
{
	if ( m_Running )
	{
		WorldManager& rWorldManager = WorldManager::GetStaticInstance();
		rWorldManager.Update();
	}
}
#endif

///////////////////////////////////////////////////////////////////////////////
// Called when an assert failure occurs
// 
void App::OnAssertFailure(const wxChar *file, int line, const wxChar *func, const wxChar *cond, const wxChar *msg)
{
	HELIUM_BREAK();
}

#if wxUSE_EXCEPTIONS

///////////////////////////////////////////////////////////////////////////////
// Called when an exception occurs in the process of dispatching events
//  It is Helium's policy to not throw C++ exceptions into wxWidgets
//  If this is a Win32/SEH exception then set your debugger to break
//   on throw instead of break on user-unhandled
// 

void App::OnUnhandledException()
{
	HELIUM_BREAK();
}

bool App::OnExceptionInMainLoop()
{
	HELIUM_BREAK();
	throw;
}

#endif

void App::SaveSettings()
{
	Helium::FilePath path;
	Helium::GetPreferencesDirectory( path );
	path += TXT("EditorSettings.json");

	std::string error;

	if ( !path.MakePath() )
	{
		error = std::string( TXT( "Could not save '" ) ) + path.c_str() + TXT( "': We could not create the directory to store the settings file." );
		wxMessageBox( error.c_str(), wxT( "Error" ), wxOK | wxCENTER | wxICON_ERROR );
		return;
	}

	if ( Helium::IsDebuggerPresent() )
	{
		Persist::ArchiveWriter::WriteToFile( path, m_SettingsManager.Ptr() );
	}
	else
	{
		if ( !Persist::ArchiveWriter::WriteToFile( path, m_SettingsManager.Ptr() ) )
		{
			error = std::string( TXT( "Could not save '" ) ) + path.c_str() + TXT( "'." );
			wxMessageBox( error.c_str(), wxT( "Error" ), wxOK | wxCENTER | wxICON_ERROR );
		}
	}
}

void App::LoadSettings()
{
	Helium::FilePath path;
	Helium::GetPreferencesDirectory( path );
	path += TXT("EditorSettings.json");

	if ( !path.Exists() )
	{
		return;
	}

	SettingsManagerPtr settingsManager = Reflect::SafeCast< SettingsManager >( Persist::ArchiveReader::ReadFromFile( path ) );
	if ( settingsManager.ReferencesObject() )
	{
		settingsManager->Clean();
		m_SettingsManager = settingsManager;
	}
	else
	{
		wxMessageBox( TXT( "Unfortunately, we could not parse your existing settings.  Your settings have been reset to defaults.  We apologize for the inconvenience." ), wxT( "Error" ), wxOK | wxCENTER | wxICON_ERROR );
	}
}

#if HELIUM_OS_WIN

static void ShowBreakpointDialog(const Helium::BreakpointArgs& args )
{
	static std::set<uintptr_t> disabled;
	static bool skipAll = false;
	bool skip = skipAll;

	// are we NOT skipping everything?
	if (!skipAll)
	{
		// have we disabled this break point?
		if (disabled.find(args.m_Info->ContextRecord->IPREG) != disabled.end())
		{
			skip = true;
		}
		// we have NOT disabled this break point yet
		else
		{
			Helium::ExceptionArgs exArgs ( Helium::ExceptionTypes::Structured, args.m_Fatal ); 
			Helium::GetExceptionDetails( args.m_Info, exArgs ); 

			// dump args.m_Info to console
			Helium::Print(Helium::ConsoleColors::Red, stderr, TXT( "%s" ), Helium::GetExceptionInfo(args.m_Info).c_str());

			// display result
			std::string message( TXT( "A break point was triggered in the application:\n\n" ) );
			message += Helium::GetSymbolInfo( args.m_Info->ContextRecord->IPREG );
			message += TXT("\n\nWhat do you wish to do?");

			const char* nothing = TXT( "Let the OS handle this as an exception" );
			const char* thisOnce = TXT( "Skip this break point once" );
			const char* thisDisable = TXT( "Skip this break point and disable it" );
			const char* allDisable = TXT( "Skip all break points" );

			wxArrayString choices;
			choices.Add(nothing);
			choices.Add(thisOnce);
			choices.Add(thisDisable);
			choices.Add(allDisable);
			wxString choice = ::wxGetSingleChoice( message.c_str(), TXT( "Break Point Triggered" ), choices );

			if (choice == nothing)
			{
				// we are not continuable, so unhook the top level filter
				SetUnhandledExceptionFilter( NULL );

				// this should let the OS prompt for the debugger
				args.m_Result = EXCEPTION_CONTINUE_SEARCH;
				return;
			}
			else if (choice == thisOnce)
			{
				skip = true;
			}
			else if (choice == thisDisable)
			{
				skip = true;
				disabled.insert(args.m_Info->ContextRecord->IPREG);
			}
			else if (choice == allDisable)
			{
				skip = true;
				skipAll = true;
			}
		}
	}

	if (skipAll || skip)
	{
		// skip break instruction (move the ip ahead one byte)
		args.m_Info->ContextRecord->IPREG += 1;

		// continue execution past the break instruction
		args.m_Result = EXCEPTION_CONTINUE_EXECUTION;
	}
	else
	{
		// fall through and let window's crash API run
		args.m_Result = EXCEPTION_CONTINUE_SEARCH;
	}
}

#endif // HELIUM_OS_WIN

namespace Helium
{
	Helium::DynamicMemoryHeap& GetComponentsDefaultHeap();
	Helium::DynamicMemoryHeap& GetEditorSupportDefaultHeap();
	Helium::DynamicMemoryHeap& GetBulletDefaultHeap();
}

///////////////////////////////////////////////////////////////////////////////
// A top level routine to parse arguments before we boot up wx via our
//  custom exception-handling entry points
// 
int Main( int argc, const char** argv )
{
	Helium::GetComponentsDefaultHeap();
	Helium::GetEditorSupportDefaultHeap();
	Helium::GetBulletDefaultHeap();

	std::vector< std::string > options;
	for ( int i = 1; i < argc; ++i )
	{
		options.push_back( argv[ i ] );
	}
	std::vector< std::string >::const_iterator argsBegin = options.begin(), argsEnd = options.end();

	bool success = true;
	std::string error; 

	Processor processor( TXT( "Helium-Tools-Editor" ), TXT( "[COMMAND <ARGS>]" ), TXT( "Editor (c) 2010 - Helium" ) );

	ProfileDumpCommand profileDumpCommand;
	success &= profileDumpCommand.Initialize( error );
	success &= processor.RegisterCommand( &profileDumpCommand, error );

	Helium::CommandLine::HelpCommand helpCommand;
	helpCommand.SetOwner( &processor );
	success &= helpCommand.Initialize( error );
	success &= processor.RegisterCommand( &helpCommand, error );

	success &= processor.AddOption( new FlagOption( &g_HelpFlag, TXT( "h|help" ), TXT( "print program usage" ) ), error );
	success &= processor.AddOption( new FlagOption( &g_DisableTracker, TXT( "disable_tracker" ), TXT( "disable Asset Tracker" ) ), error );
	success &= processor.ParseOptions( argsBegin, argsEnd, error );

	if ( success )
	{
		if ( g_HelpFlag )
		{
			// TODO: This needs to be a message box, it will never be seen in release builds
			Log::Print( TXT( "\nPrinting help for Editor...\n" ) );
			Log::Print( processor.Help().c_str() );
			Log::Print( TXT( "\n" ) );
			success = true;
		}
		else if ( argsBegin != argsEnd )
		{
			while ( success && ( argsBegin != argsEnd ) )
			{
				const std::string& arg = (*argsBegin);
				++argsBegin;

				if ( arg.length() < 1 )
				{
					continue;
				}

				if ( arg[ 0 ] == '-' )
				{
					error = TXT( "Unknown option, or option passed out of order: " ) + arg;
					success = false;
				}
				else
				{
					Command* command = processor.GetCommand( arg );
					if ( command )
					{
						success = command->Process( argsBegin, argsEnd, error );
					}
					else
					{
						error = TXT( "Unknown commandline parameter: " ) + arg + TXT( "\n\n" );
						success = false;
					}
				}
			}
		}
		else
		{
#if HELIUM_OS_WIN
			HELIUM_CONVERT_TO_CHAR( ::GetCommandLineW(), convertedCmdLine );
			return wxEntry( ::GetModuleHandle(NULL), NULL, convertedCmdLine, SW_SHOWNORMAL );
#else // HELIUM_OS_WIN
			return wxEntry( argc, const_cast<char**>( argv ) );
#endif // HELIUM_OS_WIN
		}
	}

	if ( !success && !error.empty() )
	{
		Log::Error( TXT( "%s\n" ), error.c_str() );
	}

	return success ? 0 : 1;
}


///////////////////////////////////////////////////////////////////////////////
// The actual os entry point function
// 
#if HELIUM_OS_WIN
int wmain( int argc, const wchar_t** argv )
#else // HELIUM_OS_WIN
int main( int argc, const char* argv[] )
#endif // HELIUM_OS_WIN
{
	int result = 0;

#if HELIUM_OS_WIN
	
	// convert wchar_t argc/argv into UTF-8
	std::vector< std::string > strings;
	const char** av = (const char**)alloca( argc * sizeof( const char* ) );
	for ( int i=0; i<argc; i++ )
	{
		strings.push_back( std::string() );
		ConvertString( argv[i], strings.back() );
		av[i] = strings.back().c_str();
	}

	// attach a callback to the global breakpoint exception event
	Helium::g_BreakpointOccurred.Set( &ShowBreakpointDialog );

	result = Helium::StandardMain( &Main, argc, av );

	// release our callback for handling breakpoint exceptions
	Helium::g_BreakpointOccurred.Clear();

#else // HELIUM_OS_WIN

	// hooray for UTF-8 sanity on the part of all non-windows os-es
	result = Helium::StandardMain( &Main, argc, argv );

#endif // HELIUM_OS_WIN

	return result;
}
