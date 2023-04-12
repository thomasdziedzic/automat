'''Contains the graph of recipes to build Automaton.'''

from asyncio import subprocess
from . import args
from . import cc
from . import make
from . import fs_utils
import os
import functools
import re
import shutil
import json

from .make import Popen
from subprocess import run
from pathlib import Path
from dataclasses import dataclass
from sys import platform


def is_tool(name):
    return shutil.which(name) is not None

if is_tool('clang++') and is_tool('clang') and is_tool('llvm-cxxfilt'):
    CXX = os.environ['CXX'] = f'clang++'
    CC = os.environ['CC'] = f'clang'
    CXXFILT = f'llvm-cxxfilt'
else:
    raise 'Couldn\'t find `clang`, `clang++` or `llvm-cxxfilt` on the system PATH. Make sure to install LLVM & add it to the system PATH variable!'

def libname(name):
    if platform == 'win32':
        return name + '.lib'
    else:
        return 'lib' + name + '.a'

# TODO: when on Win32 - make sure that cmake, ninja, gdb are on the PATH

CXXFLAGS = '-std=c++2b -fcolor-diagnostics -flto -Ivendor'.split()
CXXFLAGS += ['-I', str(fs_utils.project_tmp_dir)]
LDFLAGS = ['-fuse-ld=lld']
BIN_DEPS = []

if args.verbose:
    CXXFLAGS.append('-v')

if args.release:
    CXXFLAGS += ['-O3']

if args.debug:
    CXXFLAGS += ['-O0', '-g']

CXXFLAGS += ['-D' + d for d in cc.defines]

recipe = make.Recipe()

###############################
# Recipes for vendored dependencies
###############################


if args.release and args.debug:
    CMAKE_BUILD_TYPE = 'RelWithDebInfo'
elif args.release:
    CMAKE_BUILD_TYPE = 'Release'
elif args.debug:
    CMAKE_BUILD_TYPE = 'Debug'
else:
    CMAKE_BUILD_TYPE = 'MinSizeRel'

cmake_args = ['cmake', '-G', 'Ninja', f'-D{CMAKE_BUILD_TYPE=}',
              f'-DCMAKE_C_COMPILER={CC}', f'-DCMAKE_CXX_COMPILER={CXX}']

# CMake needs this policy to be enabled to respect `CMAKE_MSVC_RUNTIME_LIBRARY`
# https://cmake.org/cmake/help/latest/policy/CMP0091.html
# When vk-bootstrap sets minimum CMake version >= 3.15, the policy define can be removed.
CMAKE_MSVC_RUNTIME_LIBRARY = 'MultiThreadedDebug' if args.debug else 'MultiThreaded'
cmake_args += ['-DCMAKE_POLICY_DEFAULT_CMP0091=NEW',
               f'-D{CMAKE_MSVC_RUNTIME_LIBRARY=}']

# GoogleTest

GOOGLETEST_SRC = fs_utils.project_root / 'vendor' / 'googletest-1.13.0'
GOOGLETEST_OUT = fs_utils.project_tmp_dir / 'googletest'

GMOCK_LIB = GOOGLETEST_OUT / 'lib' / 'gmock.lib'
GTEST_LIB = GOOGLETEST_OUT / 'lib' / 'gtest.lib'
GTEST_MAIN_LIB = GOOGLETEST_OUT / 'lib' / 'gtest_main.lib'

recipe.add_step(
    functools.partial(Popen, cmake_args +
                      ['-S', GOOGLETEST_SRC, '-B', GOOGLETEST_OUT]),
    outputs=[GOOGLETEST_OUT / 'build.ninja'],
    inputs=[GOOGLETEST_SRC / 'CMakeLists.txt'],
    name='Configure GoogleTest')

recipe.add_step(
    functools.partial(Popen, ['ninja', '-C', str(GOOGLETEST_OUT)]),
    outputs=[GMOCK_LIB, GTEST_LIB, GTEST_MAIN_LIB],
    inputs=[GOOGLETEST_OUT / 'build.ninja'],
    name='Build GoogleTest')

recipe.generated.add(GOOGLETEST_OUT)

CXXFLAGS += ['-I', GOOGLETEST_SRC / 'googlemock' / 'include']
CXXFLAGS += ['-I', GOOGLETEST_SRC / 'googletest' / 'include']
LDFLAGS += ['-L', GOOGLETEST_OUT / 'lib']

# vk-bootstrap

VK_BOOTSTRAP_ROOT = fs_utils.project_tmp_dir / 'vk-bootstrap'
VK_BOOTSTRAP_BUILD = VK_BOOTSTRAP_ROOT / 'build' / CMAKE_BUILD_TYPE
VK_BOOTSTRAP_INCLUDE = VK_BOOTSTRAP_ROOT / 'src'
VK_BOOTSTRAP_LIB = VK_BOOTSTRAP_BUILD / libname('vk-bootstrap')

CXXFLAGS += ['-I', VK_BOOTSTRAP_INCLUDE]
LDFLAGS += ['-L', VK_BOOTSTRAP_BUILD]
LDFLAGS += ['-lvk-bootstrap']

recipe.add_step(
    functools.partial(Popen, [
                      'git', 'clone', 'https://github.com/charles-lunarg/vk-bootstrap', VK_BOOTSTRAP_ROOT]),
    outputs=[VK_BOOTSTRAP_ROOT / 'CMakeLists.txt', VK_BOOTSTRAP_INCLUDE],
    inputs=[],
    name='Fetch vk-bootstrap')

recipe.add_step(
    functools.partial(Popen, cmake_args +
                      ['-S', VK_BOOTSTRAP_ROOT, '-B', VK_BOOTSTRAP_BUILD]),
    outputs=[VK_BOOTSTRAP_BUILD / 'build.ninja'],
    inputs=[VK_BOOTSTRAP_ROOT / 'CMakeLists.txt'],
    name='Configure vk-bootstrap')

recipe.add_step(
    functools.partial(Popen, ['ninja', '-C', str(VK_BOOTSTRAP_BUILD)]),
    outputs=[VK_BOOTSTRAP_LIB],
    inputs=[VK_BOOTSTRAP_BUILD / 'build.ninja'],
    name='Build vk-bootstrap')

# Skia

if platform == 'win32':
    # TODO: steps for building Skia
    # Note that Skia built for Automat is modified to search for icudtl.dat in
    # C:\Windows\Globalization\ICU\
    if args.debug:
        SKIA_VARIANT = 'Debug'
    else:
        SKIA_VARIANT = 'Release'
    SKIA_ROOT = Path('C:\\Users\\maf\\skia')
    SKIA_LIB = SKIA_ROOT / 'out' / SKIA_VARIANT
    CXXFLAGS += ['-I', SKIA_ROOT]
    CXXFLAGS += ['-I', SKIA_ROOT / 'include' / 'third_party' / 'vulkan']
    LDFLAGS += ['-L', SKIA_LIB]

    # TODO: allow translation modules to request libraries
    LDFLAGS += ['-luser32', '-lopengl32', '-lgdi32']
elif platform == 'linux':
    CXXFLAGS += ['-I/home/maf/Pulpit/skia/']
    CXXFLAGS += ['-I/home/maf/Pulpit/skia/include/third_party/vulkan']
    LDFLAGS += ['-L/home/maf/Pulpit/skia/out/Static']
    LDFLAGS += ['-lfontconfig', '-lfreetype', '-lGL']
LDFLAGS += ['-lskia', '-lskottie', '-lsksg']

OBJ_DIR = fs_utils.project_tmp_dir / 'obj'
OBJ_DIR.mkdir(parents=True, exist_ok=True)

###############################
# Recipes for embedded assets
###############################

cc.reset()
cc.scan(fs_utils.project_root / 'src')

ASSETS_DIR = fs_utils.project_root / 'assets'
GENERATED_DIR = fs_utils.project_tmp_dir / 'generated'
GENERATED_DIR.mkdir(parents=True, exist_ok=True)

asset_srcs = list(ASSETS_DIR.glob('*'))
asset_objs = []

assets_h = GENERATED_DIR / 'assets.h'
assets_cc = assets_h.with_suffix('.cc')

def generate_asset_header(asset, header, extra_args):
    with header.open('w') as f:
        print('#pragma once\n', file=f)
        print('extern "C" {', file=f)
        size = asset.stat().st_size
        rel_path = str(asset.relative_to(ASSETS_DIR))
        rel_path = re.sub(r'[^a-zA-Z0-9]', '_', rel_path)
        print(f'extern char _binary_{rel_path}_start[{size}];', file=f)
        #print(f'extern char *_binary_{rel_path}_end;', file=f)
        #print(f'extern size_t _binary_{rel_path}_size;', file=f)
        print('} // extern "C"', file=f)


cc.add_header('generated/assets.h')
cc.add_translation_unit('generated/assets.cc')
cc.depends('generated/assets.cc', 'generated/assets.h')

for asset in asset_srcs:
    obj = OBJ_DIR / asset.with_suffix('.o').name
    header_abs = assets_h.with_stem(asset.stem)
    asset_objs.append(obj)
    recipe.add_step(
        functools.partial(Popen, ['llvm-objcopy', '-I', 'binary', '-O', 'elf64-x86-64', asset.relative_to(ASSETS_DIR), obj], cwd=ASSETS_DIR),
        outputs=[str(obj)],
        inputs=[str(asset)],
        name=obj.name)
    recipe.generated.add(str(obj))
    recipe.add_step(
        functools.partial(generate_asset_header, asset, header_abs),
        outputs=[str(header_abs)],
        inputs=[str(asset)],
        name=header_abs.name)
    recipe.generated.add(str(header_abs))

    header_rel = str(header_abs.relative_to(fs_utils.project_tmp_dir))
    cc.add_header(header_rel)
    cc.add_object(Path(header_rel).with_suffix('.o'))
    cc.depends('generated/assets.cc', header_rel)

def generate_assets(extra_args):
    with assets_h.open('w') as f:
        print('#pragma once\n\n#include <cstddef>', file=f)
        print('\nnamespace automaton::assets {\n', file=f)
        for asset in asset_srcs:
            size = asset.stat().st_size
            rel_path = str(asset.relative_to(ASSETS_DIR))
            rel_path = re.sub(r'[^a-zA-Z0-9]', '_', rel_path)
            print(f'extern char* {rel_path};', file=f)
            print(f'constexpr size_t {rel_path}_size = {size};', file=f)

        print('\n} // namespace automaton::assets\n', file=f)

    with assets_cc.open('w') as f:
        print('#include "assets.h"\n', file=f)
        for asset in asset_srcs:
            header = assets_h.with_stem(asset.stem)
            print(f'#include "{header.name}"', file=f)
        print('\nnamespace automaton::assets {\n', file=f)
        for asset in asset_srcs:
            rel_path = str(asset.relative_to(ASSETS_DIR))
            rel_path = re.sub(r'[^a-zA-Z0-9]', '_', rel_path)
            print(f'char *{rel_path} = _binary_{rel_path}_start;', file=f)
        print('\n} // namespace automaton::assets', file=f)

recipe.add_step(
    generate_assets,
    outputs=[str(assets_h), str(assets_cc)],
    inputs=[str(x) for x in asset_srcs],
    name='assets')

recipe.generated.add(assets_h)
recipe.generated.add(assets_cc)

cc.propagate_deps()

if args.verbose:
    cc.print_debug()

###############################
# Recipes for object files
###############################

if platform == 'win32':
    MANIFEST_RC = fs_utils.project_root / 'src' / 'manifest.rc'
    MANIFEST_RES = OBJ_DIR / 'manifest.res'
    recipe.add_step(
        functools.partial(
            Popen, ['llvm-rc', MANIFEST_RC, '/FO', MANIFEST_RES]),
        outputs=[MANIFEST_RES],
        inputs=[MANIFEST_RC],
        name='Build manifest.res')
    recipe.generated.add(MANIFEST_RES)
    LDFLAGS += [MANIFEST_RES]
    BIN_DEPS += [MANIFEST_RES]


def cxxfilt(line):
    cxx_identifier_re = "(_Z[a-zA-Z0-9_]+)"
    while match := re.search(cxx_identifier_re, line):
        cxx_identifier = match.group(1)
        try:
            demangled = run([CXXFILT, cxx_identifier], stdout=subprocess.PIPE,
                            check=True).stdout.decode('utf-8').strip()
            # add ANSI bold escape sequence
            demangled = f'\033[90m{demangled}\033[0m'
            line = line.replace(cxx_identifier, demangled)
        except:
            break
    line = line.replace(
        'std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char>>', 'string')
    line = line.replace(
        'std::basic_string_view<char, std::char_traits<char>>', 'string_view')
    line = line.replace('automaton::', '')
    line = line.replace('/usr/bin/ld: ', '')
    line = line.replace(str(OBJ_DIR), '')
    # remove module names
    while match := re.search('@[a-zA-Z0-9_]+', line):
        line = line.replace(match.group(0), '')
    # remove repetitions of type names like 'X[X]'
    line = re.sub(r'(.+)\[(\1)\]', r'\1', line)
    # remove offsets like '[filename:](.text[.symbol]+0x123):'
    line = re.sub(r'(.*:)?\(\.text(.+)?\+0x[0-9a-f]+\):', ' ', line)
    return line


def redirect_path(path):
    name = Path(path).name
    if cc.types[path] == 'object file':
        return str(OBJ_DIR / name)
    elif cc.types[path] in ('test', 'main'):
        return name
    elif path.startswith('generated'):
        return str(fs_utils.project_tmp_dir / path)
    else:
        return path


types = dict()
graph = dict()
for path, deps in cc.graph.items():
    graph[redirect_path(path)] = [redirect_path(d) for d in deps]

for path, t in cc.types.items():
    types[redirect_path(path)] = t


@dataclass
class CompilationEntry:
    file: str
    output: str
    arguments: list


compilation_db = []

for path, deps in graph.items():
    if path in recipe.generated:
        continue # skip files generated by other recipes
    t = types[path]
    pargs = [CXX] + CXXFLAGS
    if t in ('header', 'translation unit'):
        pass
    elif t == 'object file':
        recipe.generated.add(path)
        source_files = [d for d in deps if types[d] == 'translation unit']
        assert len(source_files) == 1, f'{path} has {len(source_files)} source files'
        pargs += source_files
        pargs += ['-c', '-o', path]
        builder = functools.partial(Popen, pargs)
        recipe.add_step(builder, outputs=[
                        path], inputs=deps + [VK_BOOTSTRAP_INCLUDE], name=Path(path).name)
        compilation_db.append(CompilationEntry(source_files[0], path, pargs))
    elif t == 'test':
        binary_name = Path(path).stem
        recipe.generated.add(path)
        pargs += deps + ['-o', path] + LDFLAGS + \
            ['-lgtest_main', '-lgtest', '-lgmock']
        builder = functools.partial(Popen, pargs)
        recipe.add_step(builder, outputs=[path], inputs=deps + [GMOCK_LIB, GTEST_LIB,
                        GTEST_MAIN_LIB] + BIN_DEPS, name=f'link {binary_name}', stderr_prettifier=cxxfilt)
        runner = functools.partial(Popen, [f'./{path}', '--gtest_color=yes'])
        recipe.add_step(runner, outputs=[], inputs=[path], name=binary_name)
    elif t == 'main':
        binary_name = Path(path).stem
        recipe.generated.add(path)
        pargs += deps + ['-o', path] + LDFLAGS
        builder = functools.partial(Popen, pargs)
        recipe.add_step(builder, outputs=[path], inputs=deps + [VK_BOOTSTRAP_LIB] + BIN_DEPS, name=f'link {binary_name}', stderr_prettifier=cxxfilt)
        # if platform == 'win32':
        #     MT = 'C:\\Program Files (x86)\\Windows Kits\\10\\bin\\10.0.19041.0\\x64\\mt.exe'
        #     mt_runner = functools.partial(Popen, [MT, '-manifest', 'src\win32.manifest', '-outputresource:{path}'])
        #     recipe.add_step(mt_runner, outputs=[path], inputs=[path, 'src/win32.manifest'], name=f'mt {binary_name}')
        runner = functools.partial(Popen, [f'./{path}'])
        recipe.add_step(runner, outputs=[], inputs=[path], name=binary_name)
    else:
        print(f"File '{path}' has unknown type '{types[path]}'. Dependencies:")
        for dep in deps:
            print(f'    {dep}')
        assert False

tests = [p for p, t in types.items() if t == 'test']
if tests:
    # run all tests sequentially
    def run_tests(extra_args):
        for test in tests:
            run([f'./{test}', '--gtest_color=yes'] + extra_args, check=True)
    recipe.add_step(run_tests, outputs=[], inputs=tests, name='tests')

##########################
# Recipe for Clang language server
##########################

def compile_commands(extra_args):
    print('Generating compile_commands.json...')
    jsons = []
    for entry in compilation_db:
        arguments = ',\n    '.join(json.dumps(str(arg))
                                   for arg in entry.arguments)
        json_entry = f'''{{
  "directory": { json.dumps(str(fs_utils.project_root)) },
  "file": { json.dumps(entry.file) },
  "output": { json.dumps(entry.output) },
  "arguments": [{arguments}]
}}'''
        jsons.append(json_entry)
    with open('compile_commands.json', 'w') as f:
        print('[' + ', '.join(jsons) + ']', file=f)

recipe.add_step(compile_commands, ['compile_commands.json'], [])

##########################
# Recipe for deploying to server
##########################

def deploy(extra_args):
    return Popen(['rsync', '-av', '--delete', '--no-owner', '--no-group', 'www/', 'home:/var/www/html/automat/'])

recipe.add_step(deploy, [], ['www/'])

if args.verbose:
    print('Build graph')
    for step in recipe.steps:
        print(' Step', step.name)
        print('  Inputs:')
        for inp in sorted(str(x) for x in step.inputs):
            print('    ', inp)
        print('  Outputs: ', step.outputs)
