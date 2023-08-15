#include <string>
#include <filesystem>
#include <sstream>
#include <fstream>
#include "Core.h"
#include "lua/LuaLoadLibrary.h"
#include "lua/LuaUtil.h"
#include "lua/LuaCustomOpenFile.h"
#include "lua/LuaListDirectories.h"
#include "lua/yaml/LuaYamlParser.h"
#include "lua/yaml/LuaYamlDumper.h"
#include "RuntimePatchingSystem.h"

#define LOGURU_WITH_STREAMS 1
#include "loguru.cpp"

#include "compilation/fasm.h"

#include "security/Hash.h"
#include "security/Store.h"
#include "lua/Preload.h"
#include "io/modules/ModuleHandle.h"

int luaRegisterPathAlias(lua_State* L) {
	std::string alias = luaL_checkstring(L, 1);
	std::string target = luaL_checkstring(L, 2);

	bool overwrite = false;

	if (lua_gettop(L) == 3) {
		luaL_checktype(L, 3, LUA_TBOOLEAN);
		overwrite = lua_toboolean(L, 3);
	}

	if (Core::getInstance().aliasedPaths.count(alias) > 0) {
		if (!overwrite) {
			return luaL_error(L, ("cannot overwrite alias registratrion for path: " + alias + " with: " + target + ". use overwrite = true").c_str());
		}

		Core::getInstance().log(1, "alias registration overwritten for alias: " + alias + " new target: " + target);
	}

	Core::getInstance().aliasedPaths[alias] = target;

	Core::getInstance().log(2, "alias registered for: " + alias + " target: " + target);

	return 0;
}

void addUtilityFunctions(lua_State* L) {
	// Put the 'ucp.internal' on the stack
	lua_getglobal(L, "ucp"); // [ucp]
	lua_getfield(L, -1, "internal"); // [ucp, internal]

	lua_pushcfunction(L, LuaIO::luaListDirectories); // [ucp, internal, luaListDirectories]
	lua_setfield(L, -2, "listDirectories"); // [ucp, internal]

	lua_pushcfunction(L, LuaUtil::luaWideCharToMultiByte);
	lua_setfield(L, -2, "WideCharToMultiByte");

	lua_pushcfunction(L, luaAssemble);
	lua_setfield(L, -2, "assemble");

	lua_pushcfunction(L, LuaUtil::luaGetCurrentThreadID);
	lua_setfield(L, -2, "GetCurrentThreadID");

	lua_pushcfunction(L, luaRegisterPathAlias);
	lua_setfield(L, -2, "registerPathAlias");

	lua_pop(L, 2); // pop table "internal" and pop table "ucp": []
}

const struct luaL_Reg RPS_LIB[] = {

	{NULL, NULL}
};

void addIOFunctions(lua_State* L) {
	lua_pushglobaltable(L);
	
	lua_pushcfunction(L, LuaIO::luaLoadLibrary);
	lua_setfield(L, -2, "loadLibrary");


	lua_getfield(L, -1, "io");

	lua_pushcfunction(L, LuaIO::luaIOCustomOpen);
	lua_setfield(L, -2, "open");

	lua_pushcfunction(L, LuaIO::luaIOCustomOpenFilePointer);
	lua_setfield(L, -2, "openFilePointer");

	lua_pushcfunction(L, LuaIO::luaIOCustomOpenFileDescriptor);
	lua_setfield(L, -2, "openFileDescriptor");


	// Create ucrt subtable
	lua_newtable(L);

	lua_pushinteger(L, (DWORD) &_read); lua_setfield(L, -2, "_read"); //
	lua_pushinteger(L, (DWORD)&_close); lua_setfield(L, -2, "_close"); //
	lua_pushinteger(L, (DWORD)&_open); lua_setfield(L, -2, "_open"); //
	lua_pushinteger(L, (DWORD)&_write); lua_setfield(L, -2, "_write"); //
	lua_pushinteger(L, (DWORD)&_flushall); lua_setfield(L, -2, "_flushall"); //
	lua_pushinteger(L, (DWORD)&_lseek); lua_setfield(L, -2, "_lseek"); //
	lua_pushinteger(L, (DWORD)&_tell); lua_setfield(L, -2, "_tell"); //
	lua_pushinteger(L, (DWORD)&_fileno); lua_setfield(L, -2, "_fileno"); //
	lua_pushinteger(L, (DWORD)&_fsopen); lua_setfield(L, -2, "_fsopen"); //

	lua_pushinteger(L, (DWORD)&fopen); lua_setfield(L, -2, "fopen"); //
	lua_pushinteger(L, (DWORD)&fclose); lua_setfield(L, -2, "fclose"); //
	lua_pushinteger(L, (DWORD)&fread); lua_setfield(L, -2, "fread"); //
	lua_pushinteger(L, (DWORD)&fread_s); lua_setfield(L, -2, "fread_s"); //
	lua_pushinteger(L, (DWORD)&fwrite); lua_setfield(L, -2, "fwrite");
	lua_pushinteger(L, (DWORD)&fseek); lua_setfield(L, -2, "fseek"); //
	lua_pushinteger(L, (DWORD)&ftell); lua_setfield(L, -2, "ftell"); //
	lua_pushinteger(L, (DWORD)&fflush); lua_setfield(L, -2, "fflush"); //
	lua_pushinteger(L, (DWORD)&fgetpos); lua_setfield(L, -2, "fgetpos"); //
	lua_pushinteger(L, (DWORD)&fsetpos); lua_setfield(L, -2, "fsetpos"); //
	lua_pushinteger(L, (DWORD)&getwc); lua_setfield(L, -2, "getwc"); //

	// Set the table
	lua_setfield(L, -2, "ucrt");
	

	lua_pop(L, 1); // Pop the io table


	// Create Yaml table
	lua_createtable(L, 0, 2);

	lua_pushcfunction(L, LuaYamlParser::luaParseYamlContent);
	lua_setfield(L, -2, "parse");
	lua_pushcfunction(L, LuaYamlParser::luaParseYamlContent);
	lua_setfield(L, -2, "eval"); // FOr backwards compatilibity

	lua_pushcfunction(L, LuaYamlDumper::luaDumpLuaTable);
	lua_setfield(L, -2, "dump");
	
	// store table as a global yaml table
	lua_setglobal(L, "yaml");
	//lua_pop(L, 1);

	/**
	 * The code below is also possible.
	lua_pushcfunction(L, LuaIO::luaScopedRequire);
	lua_setfield(L, -2, "require"); //Overriding the global require
	
	* But we can also do this: */
	if (luaL_loadbufferx(L, ucp_code_pre.c_str(), ucp_code_pre.size(), "ucp/code/pre.lua", "t") != LUA_OK) {
		std::cout << "ERROR in loading pre.lua" << lua_tostring(L, -1) << std::endl;
		lua_pop(L, 1);
	}
	else {
		if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
			std::cout << "ERROR in executing pre.lua: " << lua_tostring(L, -1) << std::endl;
			lua_pop(L, 1);
		};
	}

	lua_pop(L, 1); //Pop the global table
}

void addUCPInternalFunctions(lua_State* L) {
	lua_newtable(L);
	RPS_initializeLuaAPI("");
	// The namespace is left on the stack. 

	// Set the namespace to the 'internal' field in our table.
	lua_setfield(L, -2, "internal");
	// Our table is left on the stack. Put the table in the global 'ucp' variable.
	lua_setglobal(L, "ucp");
}

void logToStdOut(void* user_data, const loguru::Message& message) {
	std::cout << message.message << std::endl;
}

static int luaPrint(lua_State* L) {
	std::stringstream output;

	int n = lua_gettop(L);  /* number of arguments */
	int i;
	for (i = 1; i <= n; i++) {  /* for each argument */
		size_t l;
		const char* s = luaL_tolstring(L, i, &l);  /* convert it to string */
		if (i > 1)  /* not the first element? */
			output << "\t";
		output << s;
		lua_pop(L, 1);  /* pop result */
	}
	LOG_S(INFO) << output.str();
	return 0;
}

static const struct luaL_Reg printlib[] = {
  {"print", luaPrint},
  {NULL, NULL} /* end of array */
};

void initializeLogger(int logLevel) {
	// Put every log message of at least logLevel in ucp3.log
	loguru::add_file("ucp3.log", loguru::Truncate, logLevel);

	// Only log WARNING, ERROR and FATAL to "error.log":
	loguru::add_file("ucp3-error-log.log", loguru::Truncate, loguru::Verbosity_WARNING);

	// Also log logLevel and higher to the console
	loguru::add_callback("stdout", logToStdOut, NULL, logLevel);

	// Only show most relevant things on stderr:
	loguru::g_stderr_verbosity = loguru::Verbosity_MAX;
}

void deinitializeLogger() {
	loguru::shutdown();
}

int luaLog(lua_State* L) {
	if (lua_gettop(L) < 2) {
		return luaL_error(L, "invalid number of arguments, expected at least 2: log level, message, ...");
	}
	int logLevel = luaL_checkinteger(L, 1);

	std::stringstream output;

	int n = lua_gettop(L);  /* number of arguments */
	int i;
	const int start = 2;
	for (i = start; i <= n; i++) {  /* for each argument */
		size_t l;
		const char* s = luaL_tolstring(L, i, &l);  /* convert it to string */
		if (i > start)  /* not the first element? */
			output << "\t";
		output << s;
		lua_pop(L, 1);  /* pop result */
	}

	// If log is worse than warning
	if (logLevel < loguru::Verbosity_WARNING) {
		MessageBoxA(0, output.str().c_str(), "Error", MB_OK);
	}

	VLOG_F(logLevel, output.str().c_str());

	return 0;
}

void addLoggingFunctions(lua_State* L) {
	lua_pushglobaltable(L);
	luaL_setfuncs(L, printlib, 0);
	lua_pop(L, 1);

	// Put the 'ucp.internal' on the stack
	lua_getglobal(L, "ucp"); // [ucp]
	lua_getfield(L, -1, "internal"); // [ucp, internal]

	lua_pushcfunction(L, luaLog); // [ucp, internal, luaListDirectories]
	lua_setfield(L, -2, "log"); // [ucp, internal]

	lua_pop(L, 2); // pop table "internal" and pop table "ucp": []
}

void Core::log(int logLevel, std::string message) {
	VLOG_F(logLevel, message.c_str());
}

bool Core::sanitizePath(const std::string& path, std::string& result) {
	std::string rawPath = path;

	//Assert non empty path
	if (rawPath.empty()) {
		result = "empty path";
		return false;
	}

	std::filesystem::path sanitizedPath(rawPath);
	sanitizedPath = sanitizedPath.lexically_normal(); // Remove "/../" and "/./"

	if (!std::filesystem::path(sanitizedPath).is_relative()) {
		result = "path has to be a relative path";
		return false;
	}

	result = sanitizedPath.string();

	//Now we can assume sanitizedPath cannot escape the game directory.
//Let's assert that
	std::filesystem::path a = std::filesystem::current_path();
	std::filesystem::path b = a / result;
	std::filesystem::path r = std::filesystem::relative(b, a);
	if (r.string().find("..") == 0) {

		if (this->debugMode) {
			// Technically not allowed, but we will let it slip because we are debugging
			LOG_S(1) << "the path specified is not a proper relative path. Is it escaping the game directory? path: " << std::endl << r.string();
		}
		else {
			LOG_S(WARNING) << "the path specified is not a proper relative path. Is it escaping the game directory? path: " << std::endl << r.string();
			result = "the path specified is not a proper relative path. Is it escaping the game directory? path: " + r.string();
			return false;
		}
	}

	//Replace \\ with /. Note: don't call make_preferred on the path, it will reverse this change.
	std::replace(result.begin(), result.end(), '\\', '/');

	return true;
}



bool Core::resolveAliasedPath(std::string& path) {

	for (auto const& [alias, resolvedPath] : aliasedPaths) {
		int loc = path.rfind(alias, 0);
		if (loc == 0) {
			path = resolvedPath + path.substr(0 + alias.size());

			return true;
		}
		
	}

	return false;
}

bool Core::pathIsInPluginDirectory(const std::string& sanitizedPath, std::string& extension, std::string& basePath, std::string& insideExtensionPath) {

	std::regex re("^ucp/+plugins/+([A-Za-z0-9_.-]+)/+(.*)$");
	std::filesystem::path path(sanitizedPath);

	if (sanitizedPath.find("ucp/plugins/") == 0 || sanitizedPath == "ucp/plugins/") {
		std::smatch m;
		if (std::regex_search(sanitizedPath, m, re)) {
			extension = m[1];
			insideExtensionPath = m[2];
			basePath = (Core::getInstance().UCP_DIR / "plugins" / extension).string();
			return true;

		}
		return false;
	}

	return false;
}

bool Core::pathIsInModuleDirectory(const std::string& sanitizedPath, std::string& extension, std::string& basePath, std::string& insideExtensionPath) {

	std::regex re("^ucp/+modules/+([A-Za-z0-9_.-]+)/+(.*)$");
	std::filesystem::path path(sanitizedPath);

	if (sanitizedPath.find("ucp/modules/") == 0 || sanitizedPath == "ucp/modules/") {
		std::smatch m;
		if (std::regex_search(sanitizedPath, m, re)) {
			extension = m[1];
			insideExtensionPath = m[2];
			basePath = (Core::getInstance().UCP_DIR / "modules" / extension).string();
			return true;

		}
		return false;
	}

	return false;
}

bool Core::pathIsInInternalCodeDirectory(const std::string& sanitizedPath, std::string& insideCodePath) {

	std::regex re("^ucp/+code/+(.*)$");
	std::filesystem::path path(sanitizedPath);

	if (sanitizedPath.find("ucp/code/") == 0 || sanitizedPath == "ucp/code/") {
		std::smatch m;
		if (std::regex_search(sanitizedPath, m, re)) {
			insideCodePath = m[1];

			return true;

		}
		return false;
	}

	return false;
}

void Core::initialize() {

	if (this->isInitialized) {
		MessageBoxA(NULL, "UCP3 dll was already initialized", "FATAL: already initialized", MB_OK);
		LOG_S(FATAL) << "UCP3 dll was already initialized";
		return;
	}

	int verbosity = 0;
	char* ENV_UCP_VERBOSITY = std::getenv("UCP_VERBOSITY");
	if (ENV_UCP_VERBOSITY == NULL) {
		verbosity = 0;
	}
	else {
		std::istringstream s(ENV_UCP_VERBOSITY);
		s >> verbosity; // We don't care about errors at this point.
	}

	this->logLevel = verbosity;
	initializeLogger(this->logLevel);

#if !defined(_DEBUG) && defined(COMPILED_MODULES)
	// No Console
	this->hasConsole = false;
#else
	// In principle yes, unless explicitly not
	this->hasConsole = true;
	char* ENV_UCP_CONSOLE = std::getenv("UCP_CONSOLE");
	if (ENV_UCP_CONSOLE != NULL) {
		if (std::string(ENV_UCP_CONSOLE) == "0") {
			this->hasConsole = false;
		}
	}
	if (this->hasConsole) {
		initializeConsole();
	}
//#elif !defined(COMPILED_MODULES)
//	char* ENV_UCP_CONSOLE = std::getenv("UCP_CONSOLE");
//	if (ENV_UCP_CONSOLE != NULL) {
//		if (std::string(ENV_UCP_CONSOLE) == "1") {
//			this->hasConsole = true;
//		}
//	}
//	if (this->hasConsole) {
//		initializeConsole();
//	}
#endif
		
	RPS_initializeLua();
	this->L = RPS_getLuaState();
	
	RPS_initializeLuaOpenLibs();

	// Install the print redirect to logger

	addUCPInternalFunctions(this->L);
	addLoggingFunctions(this->L);
	addUtilityFunctions(this->L);
	addIOFunctions(this->L);

	ModuleHandle* mh;

	try {
		mh = ModuleHandleManager::getInstance().getLatestCodeHandle();

		std::string mainPath = "main.lua";
		std::string error;
		FILE* f = mh->openFilePointer(mainPath, error);

		if (f == NULL) {
			MessageBoxA(0, "ERROR: failed to load ucp/code/main.lua: does not exist", "FATAL", MB_OK);
			LOG_S(FATAL) << "ERROR: failed to load ucp/code/main.lua: " << "does not exist";
		}
		else {
			std::filebuf buf(f);
			std::istream inp(&buf);
			std::stringstream buffer;
			buffer << inp.rdbuf();
			std::string code = buffer.str();
			if (code.empty()) {
				MessageBoxA(0, "Could not execute main.lua: empty file", "FATAL", MB_OK);
				LOG_S(FATAL) << "Could not execute main.lua: empty file";
			}
			else {
				if (luaL_loadbufferx(this->L, code.c_str(), code.size(), "ucp/code/main.lua", "t") != LUA_OK) {
					std::string errorMsg = std::string(lua_tostring(this->L, -1));
					lua_pop(this->L, 1);
					MessageBoxA(0, ("Failed to load main.lua: " + errorMsg).c_str(), "FATAL", MB_OK);
					LOG_S(FATAL) << "Failed to load main.lua: " << errorMsg;


				}

				// Don't expect return values
				if (lua_pcall(this->L, 0, 0, 0) != LUA_OK) {
					std::string errorMsg = std::string(lua_tostring(this->L, -1));
					lua_pop(this->L, 1);
					MessageBoxA(0, ("Failed to run main.lua: " + errorMsg).c_str(), "FATAL", MB_OK);
					LOG_S(FATAL) << "Failed to run main.lua: " << errorMsg;
				}

				LOG_S(INFO) << "Finished running bootstrap file";
			}
		}


	}
	catch (ModuleHandleException e) {
		std::string errorMsg = "ERROR: failed to load ucp/code/main.lua: does not exist";
		errorMsg += ("\n reason: " + std::string(e.what()));

		MessageBoxA(0, errorMsg.c_str(), "FATAL", MB_OK);
		LOG_S(FATAL) << errorMsg;
	}
	
	if (this->hasConsole) {
		consoleThread = CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)ConsoleThread, NULL, 0, nullptr);

		if (consoleThread == INVALID_HANDLE_VALUE) {
			MessageBoxA(NULL, std::string("Could not start thread").c_str(), std::string("...").c_str(), MB_OK);
		}
		else if (consoleThread == 0) {
			MessageBoxA(NULL, std::string("Could not start thread").c_str(), std::string("...").c_str(), MB_OK);
		}
		else {
			CloseHandle(consoleThread);
		}
	}

	this->isInitialized = true;
}

bool Core::moduleExists(const std::string moduleFullName, bool& asZip, bool& asFolder) {

	asFolder = std::filesystem::is_directory(std::filesystem::path("ucp/modules") / moduleFullName);
	asZip = std::filesystem::is_regular_file(std::filesystem::path("ucp/modules") / (moduleFullName + ".zip"));

	return asZip || asFolder;
}


bool Core::codeLocationExists(bool& asZip, bool& asFolder) {

	asFolder = std::filesystem::is_directory(std::filesystem::path("ucp/code"));
	asZip = std::filesystem::is_regular_file(std::filesystem::path("ucp/code.zip"));

	return asZip || asFolder;
}

Store Core::getModuleHashStore() {
	return *this->moduleHashStore;
}
