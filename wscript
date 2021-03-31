#! /usr/bin/env python
# encoding: utf-8
# a1batross, mittorn, 2018

from __future__ import print_function
from waflib import Logs, Context, Configure
import sys
import os

VERSION = '0.99'
APPNAME = 'source-engine'
top = '.'

FT2_CHECK='''extern "C" {
#include <ft2build.h>
#include FT_FREETYPE_H
}

int main() { return FT_Init_FreeType( NULL ); }
'''

FC_CHECK='''extern "C" {
#include <fontconfig/fontconfig.h>
}

int main() { return (int)FcInit(); }
'''

Context.Context.line_just = 55 # should fit for everything on 80x26

projects=[
	'tier0','tier1','tier2',
	'vstdlib','vpklib','filesystem'
	,'mathlib','tier3',
	'bitmap','scenefilecache','datacache',
	'launcher_main','vgui2/vgui_controls','vgui2/matsys_controls','vgui2/vgui_surfacelib',
	'serverbrowser','soundemittersystem','vgui2/src',
	'togl','vguimatsurface','vtf','materialsystem/shaderlib',
	'materialsystem','studiorender','materialsystem/stdshaders',
	'video','inputsystem','appframework',
	'launcher','engine/voice_codecs/minimp3','materialsystem/shaderapidx9',
	'gameui','dmxloader','datamodel','engine'
]

projects += ['thirdparty/StubSteamAPI'] # ,'thirdparty/libjpeg','thirdparty/SDL2-src'] # thirdparty projects

@Configure.conf
def check_pkg(conf, package, uselib_store, fragment, *k, **kw):
	errormsg = '{0} not available! Install {0} development package. Also you may need to set PKG_CONFIG_PATH environment variable'.format(package)
	confmsg = 'Checking for \'{0}\' sanity'.format(package)
	errormsg2 = '{0} isn\'t installed correctly. Make sure you installed proper development package for target architecture'.format(package)

	try:
		conf.check_cfg(package=package, args='--cflags --libs', uselib_store=uselib_store, *k, **kw )
	except conf.errors.ConfigurationError:
		conf.fatal(errormsg)

	try:
		conf.check_cxx(fragment=fragment, use=uselib_store, msg=confmsg, *k, **kw)
	except conf.errors.ConfigurationError:
		conf.fatal(errormsg2)

@Configure.conf
def get_taskgen_count(self):
	try: idx = self.tg_idx_count
	except: idx = 0 # don't set tg_idx_count to not increase counter
	return idx

def define_platform(conf):
	conf.define('SOURCE1',1)
	if conf.env.DEST_OS == 'linux':
		conf.define('_GLIBCXX_USE_CXX11_ABI',0)
		conf.env.append_unique('DEFINES', [
			'LINUX=1',
			'_LINUX=1',
			'POSIX=1',
			'_POSIX=1',
			'GNUC',
			'DX_TO_GL_ABSTRACTION',
			'GL_GLEXT_PROTOTYPES',
			'BINK_VIDEO',
			'USE_SDL',
			'NDEBUG',
			'NO_HOOK_MALLOC',
			'_DLL_EXT=.so'
		])

def options(opt):
	grp = opt.add_option_group('Common options')

	grp.add_option('-8', '--64bits', action = 'store_true', dest = 'ALLOW64', default = False,
		help = 'allow targetting 64-bit engine(Linux/Windows/OSX x86 only) [default: %default]')

	grp.add_option('-W', '--win-style-install', action = 'store_true', dest = 'WIN_INSTALL', default = False,
		help = 'install like Windows build, ignore prefix, useful for development [default: %default]')

	opt.load('compiler_optimizations subproject')

	opt.add_subproject(projects)

	opt.load('xcompile compiler_cxx compiler_c sdl2 clang_compilation_database strip_on_install waf_unit_test subproject')
	if sys.platform == 'win32':
		opt.load('msvc msdev msvs')
	opt.load('reconfigure')

def configure(conf):
	conf.env.PREFIX = ''

	conf.load('fwgslib reconfigure')

	# Force XP compability, all build targets should add
	# subsystem=bld.env.MSVC_SUBSYSTEM
	# TODO: wrapper around bld.stlib, bld.shlib and so on?
	conf.env.MSVC_SUBSYSTEM = 'WINDOWS,5.01'
	conf.env.MSVC_TARGETS = ['x86'] # explicitly request x86 target for MSVC
	if sys.platform == 'win32':
		conf.load('msvc msvc_pdb msdev msvs')
	conf.load('subproject xcompile compiler_c compiler_cxx gitversion clang_compilation_database strip_on_install waf_unit_test enforce_pic')

	conf.check_cfg(package='sdl2', uselib_store='SDL2', args=['--cflags', '--libs'])
	conf.check_cfg(package='libjpeg', uselib_store='JPEG', args=['--cflags', '--libs'])
	conf.check_cfg(package='libpng', uselib_store='PNG', args=['--cflags', '--libs'])
	conf.check_cfg(package='zlib', uselib_store='ZLIB', args=['--cflags', '--libs'])
	conf.check_cfg(package='openal', uselib_store='OPENAL', args=['--cflags', '--libs'])
	conf.check_cfg(package='libcurl', uselib_store='CURL', args=['--cflags', '--libs'])
	conf.check_cfg(package='bzip2', uselib_store='BZIP2', args=['--cflags', '--libs'])
	conf.check_pkg('freetype2', 'FT2', FT2_CHECK)
	conf.check_pkg('fontconfig', 'FC', FC_CHECK)

#	enforce_pic = True # modern defaults

	# modify options dictionary early
#	if conf.env.DEST_OS == 'android':

#	if conf.env.STATIC_LINKING:
#		enforce_pic = False # PIC may break full static builds

#	conf.check_pic(enforce_pic)

	# We restrict 64-bit builds ONLY for Win/Linux/OSX running on Intel architecture
	# Because compatibility with original GoldSrc
	if conf.env.DEST_OS in ['win32', 'linux', 'darwin'] and conf.env.DEST_CPU == 'x86_64':
		conf.env.BIT32_MANDATORY = not conf.options.ALLOW64
		if conf.env.BIT32_MANDATORY:
			Logs.info('WARNING: will build engine for 32-bit target')
	else:
		conf.env.BIT32_MANDATORY = False

	conf.load('force_32bit')

	compiler_optional_flags = [
#		'-Wall', '-Wextra', '-Wpedantic',
		'-fdiagnostics-color=always',
#		'-Werror=return-type',
#		'-Werror=parentheses',
#		'-Werror=vla',
#		'-Werror=tautological-compare',
#		'-Werror=duplicated-cond',
#		'-Werror=duplicated-branches', # BEWARE: buggy
#		'-Werror=bool-compare',
#		'-Werror=bool-operation',
		'-Wcast-align',
#		'-Werror=cast-align=strict', # =strict is for GCC >=8
#		'-Werror=packed',
#		'-Werror=packed-not-aligned',
		'-Wuninitialized', # older GCC versions have -Wmaybe-uninitialized enabled by this switch, which is not accurate
                                   # so just warn, not error
		'-Winit-self',
#		'-Werror=implicit-fallthrough=2', # clang incompatible without "=2"
#		'-Wdouble-promotion', # disable warning flood
		'-Wstrict-aliasing'
	]

	c_compiler_optional_flags = [
#		'-Werror=incompatible-pointer-types',
#		'-Werror=implicit-function-declaration',
#		'-Werror=int-conversion',
#		'-Werror=implicit-int',
#		'-Werror=strict-prototypes',
#		'-Werror=old-style-declaration',
#		'-Werror=old-style-definition',
#		'-Werror=declaration-after-statement',
		'-fnonconst-initializers', # owcc
	]

	cflags, linkflags = conf.get_optimization_flags()
	cflags += ['-march=pentium4','-mtune=core2','-mfpmath=387']
	linkflags += ['-march=pentium4','-mtune=core2','-mfpmath=387']
	# And here C++ flags starts to be treated separately
	cxxflags = list(cflags) + ['-std=c++11','-fpermissive']

	if conf.env.COMPILER_CC == 'gcc':
		wrapfunctions = ['fopen','freopen','open','creat','access','__xstat','stat','lstat','fopen64','open64',
			'opendir','__lxstat','chmod','chown','lchown','symlink','link','__lxstat64','mknod',
			'utimes','unlink','rename','utime','__xstat64','mount','mkfifo','mkdir','rmdir','scandir','realpath']

		for func in wrapfunctions:
			linkflags += ['-Wl,--wrap='+func]

		conf.define('COMPILER_GCC', 1)

	if conf.env.COMPILER_CC != 'msvc':
		conf.check_cc(cflags=cflags, linkflags=linkflags, msg='Checking for required C flags')
		conf.check_cxx(cxxflags=cxxflags, linkflags=linkflags, msg='Checking for required C++ flags')

		linkflags += ['-pthread']
		conf.env.append_unique('CFLAGS', cflags)
		conf.env.append_unique('CXXFLAGS', cxxflags)
		conf.env.append_unique('LINKFLAGS', linkflags)

		cxxflags += conf.filter_cxxflags(compiler_optional_flags, cflags)
		cflags += conf.filter_cflags(compiler_optional_flags + c_compiler_optional_flags, cflags)

	conf.env.append_unique('CFLAGS', cflags)
	conf.env.append_unique('CXXFLAGS', cxxflags)
	conf.env.append_unique('LINKFLAGS', linkflags)

	if conf.env.DEST_OS != 'win32':
		conf.check_cc(lib='dl', mandatory=False)

		if not conf.env.LIB_M: # HACK: already added in xcompile!
			conf.check_cc(lib='m')
	else:
		# Common Win32 libraries
		# Don't check them more than once, to save time
		# Usually, they are always available
		# but we need them in uselib
		a = map(lambda x: {
			# 'features': 'c',
			# 'message': '...' + x,
			'lib': x,
			# 'uselib_store': x.upper(),
			# 'global_define': False,
		}, [
			'user32',
			'shell32',
			'gdi32',
			'advapi32',
			'dbghelp',
			'psapi',
			'ws2_32'
		])

		for i in a:
			conf.check_cc(**i)

		# conf.multicheck(*a, run_all_tests = True, mandatory = True)

	# indicate if we are packaging for Linux/BSD
	if not conf.options.WIN_INSTALL and conf.env.DEST_OS not in ['win32', 'darwin', 'android']:
		conf.env.LIBDIR = conf.env.BINDIR = '${PREFIX}/lib/'
	else:
		conf.env.LIBDIR = conf.env.BINDIR = conf.env.PREFIX

	define_platform(conf)

	conf.add_subproject(projects)

def build(bld):
	bld.add_subproject(projects)
