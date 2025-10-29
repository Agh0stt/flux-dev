#					     	FLUX									#

# Official repo of a custom programming language made by (@Agh0stt) and lisenced under the MIT lisence.
this language is both compiled and interpreted, a bit like java.
the compiler (main.c / fluxc) converts the high level code into low level opcode /bytecode which is then ran by the vm
# file extentions:
*.flux - flux code files
*.fluxb - compiled flux files into bytecode
# steps:
(dependencies)::
(linux and unix):
 git clang python dartsdk/dart
 (windows):
 VS STUDIO 2022/+
 MSVC
 ## clone this repo
 git clone --depth 1 https://github.com/Agh0stt/flux-dev.git
 ## change directory:
 (unix and linux):
 cd flux-dev
 (windows)
 cd flux-dev
 ## compile:
 (unix and linux):
  dart compile exe main.dart -o fluxc
 dart compile exe vm.dart -o fluxvm
 (windows):
 dart compile exe main.dart -o fluxc.exe
 dart compile exe vm.dart -o fluxvm.exe
 ## compiling the programs:
 (linux/unix):
 ./fluxc hello.flux hello.fluxb
 ./fluxvm hello.fluxb
 (windows):
 ./fluxc.exe hello.flux hello.fluxb
 ./fluxvm.exe hello.fluxb
 ## copyright - Abhigyan Ghosh 2025- present
