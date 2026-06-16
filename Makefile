# Makefile :)

###################################################################################################
#
# This is a build system in itself atop GNU Make, aiming simplicity and easy cross-compilation
#
# The project specification (Name, sources, feature useflags, binary/library targets) resides in project.mk
#
# Built-in variables (Some are controllable from env):
#
# - BUILDDIR:   Build directory, defaults for example to `release.windows.x86_64`
#
# - CC:         C compiler
# - CXX:        C++ compiler
# - CPPFLAGS:   User-supplied additional C/C++ preprocessor flags
# - CFLAGS:     User-supplied additional C/C++ optimization/warning flags
# - LDFLAGS:    User-supplied additional linker flags
# - PKG_CONFIG: Package config tool (pkg-config)
#
# - CC_BRAND:   Compiler brand (gcc / clang / tcc / ...)
# - CC_VERSION: Compiler version
# - CC_TRIPLET: Compiler target triplet
# - CC_PRETTY:  Compiler pretty name & version
#
# - ARCH:       Target architecture
# - OS:         Target operating system (Lowercase)
# - OS_PRETTY:  Target operating system (As in uname)
# - HOST_ARCH:  Host architecture
# - HOST_OS:    Host operating system (Lowercase)
# - HOST_UNAME: Host operating system (As in uname)
#
# - GIT_DESCRIBE: Project version in the form from `git describe`, also accessible as GIT_COMMIT
#
# - USE_LTO:         Use Link-Time optimizations
# - USE_LIB:         Build dynamic libraries
# - USE_LIB_STATIC:  Build static libraries
# - USE_LIB_SHARING: Link to shared project libraries
# - USE_OBJ_STAGE:   Use intermediate object file build step
# - USE_DEBUG:       Optimized build with debug info
# - USE_DEBUG_FULL:  Full debug build without optimizations
# - USE_UBSAN:       Use UndefinedBehaviorSanitizer, implies USE_DEBUG
# - USE_ASAN:        Use AddressSanitizer, implies USE_DEBUG
# - USE_TSAN:        Use ThreadSanitizer, implies USE_DEBUG. Clang only!
# - USE_MSAN:        Use MemorySanitizer, implies USE_DEBUG. Clang only!
#
###################################################################################################

CC           ?= cc
CXX          ?= c++
PKG_CONFIG   ?= pkg-config
CPPFLAGS     ?=
CFLAGS       ?=
LDFLAGS      ?=
OS           ?=
ARCH         ?=
CC_BRAND     ?=
CC_VERSION   ?=
GIT_DESCRIBE ?=

# Default target: all
all:

# Disable build-in suffix rules
.SUFFIXES:

###################################################################################################
#
# Internal constants & functions
#
###################################################################################################

# String constants
override EMPTY :=
override SPACE := $(EMPTY) $(EMPTY)
override TAB   := $(EMPTY)	$(EMPTY)
override BSLSH := $(EMPTY)\$(EMPTY)
override COMMA := ,
override QUOT  := "
override MAGIC := ␇
override define NEWLINE


endef

# List of known vendor fields in compiler triplets
# NOTE: This might be unnecessary in most cases, but please extend this list whenever needed.
override TRIPLET_KNOWN_VENDORS := unknown pc apple amd intel suse redhat buildroot w64 uwp wrs ibm sun sgi sony

# List of operating systems that are advertised via ABI field, and should take priority when detected
# NOTE: This might be unnecessary in most cases, but please extend this list whenever needed.
override TRIPLET_KNOWN_OS_PRIO := android

# List of known non-system ABI fields in compiler triplets
# NOTE: This might be unnecessary in most cases, but please extend this list whenever needed.
override TRIPLET_KNOWN_ABI_STR := abi gnu musl uclibc elf

# List of known compiler brands advertised in $(CC) -v for fallback compiler detection
override CC_KNOWN_BRANDS := clang gcc oneAPI cosmocc cproc tcc xcc

# Generate a dependency on $2 for target $1
override gen_dependency = $(if $1,$(if $2,$1: $2))

# Checks whether the input consists exactly of a single word, returns it if so
override var_single = $(if $(filter 1,$(words $1)),$1)

# Get variable if it's actually defined without tripping --warn-undefined-variables
override var_def = $(if $(filter undefined,$(origin $1)),,$($1))

# Get variable if it's specifically defined in a Makefile source and not leaking in from env
override var_src = $(if $(filter-out file override default automatic,$(origin $1)),,$($1))

# Get variable if it's defined to a non-zero value
override var_use = $(filter-out 0,$(firstword $(call var_def,$(filter-out $(call var_src,USEFLAGS_OFF),$1))))

# Escape GNU Make variable into a single word
override make_esc = $(subst $(SPACE),$(MAGIC)SPACE,$(subst $(NEWLINE),$(MAGIC)NEWLINE,$1))

# Unescape GNU Make variable
override make_unesc = $(subst $(MAGIC)SPACE,$(SPACE),$(subst $(MAGIC)NEWLINE,$(NEWLINE),$1))

# Check whether variable $1 is equal to $2
override equal = $(if $(filter $(call make_esc,$1)$(MAGIC)END,$(call make_esc,$2)$(MAGIC)END),eq)

# Check whether variable $1 is not equal to $2
override nonequal = $(if $(call equal,$1,$2),,ne)

# Much like $(findstring find,in) but allows passing multiple words for matching
override find_any_str = $(strip $(foreach find,$1,$(findstring $(find),$2)))

# Lower-case the string
override tolower = $(subst A,a,$(subst B,b,$(subst C,c,$(subst D,d,$(subst E,e,$(subst F,f,$(subst G,g,$(subst H,h,$(subst I,i,$(subst J,j,$(subst K,k,$(subst L,l,$(subst M,m,$(subst N,n,$(subst O,o,$(subst P,p,$(subst Q,q,$(subst R,r,$(subst S,s,$(subst T,t,$(subst U,u,$(subst V,v,$(subst W,w,$(subst X,x,$(subst Y,y,$(subst Z,z,$1))))))))))))))))))))))))))

# Upper-case the string
override toupper = $(subst a,A,$(subst b,B,$(subst c,C,$(subst d,D,$(subst e,E,$(subst f,F,$(subst g,G,$(subst h,H,$(subst i,I,$(subst j,J,$(subst k,K,$(subst l,L,$(subst m,M,$(subst n,N,$(subst o,O,$(subst p,P,$(subst q,Q,$(subst r,R,$(subst s,S,$(subst t,T,$(subst u,U,$(subst v,V,$(subst w,W,$(subst x,X,$(subst y,Y,$(subst z,Z,$1))))))))))))))))))))))))))

# Capitalize the string
override capitalize = $(patsubst a%,A%,$(patsubst b%,B%,$(patsubst c%,C%,$(patsubst d%,D%,$(patsubst e%,E%,$(patsubst f%,F%,$(patsubst g%,G%,$(patsubst h%,H%,$(patsubst i%,I%,$(patsubst j%,J%,$(patsubst k%,K%,$(patsubst l%,L%,$(patsubst m%,M%,$(patsubst n%,N%,$(patsubst o%,O%,$(patsubst p%,P%,$(patsubst q%,Q%,$(patsubst r%,R%,$(patsubst s%,S%,$(patsubst t%,T%,$(patsubst u%,U%,$(patsubst v%,v%,$(patsubst w%,W%,$(patsubst x%,X%,$(patsubst y%,Y%,$(patsubst z%,Z%,$1))))))))))))))))))))))))))

# Check whether this is a numeric string (But possibly with trailing garbage), returns it if so
override is_numeric = $(filter 0% 1% 2% 3% 4% 5% 6% 7% 8% 9%,$1)

# Check whether this may be a semantic version string (Starts with a numeric literal and has dots), returns it if so
override is_semver = $(if $(findstring .,$1),$(call is_numeric,$1))

# Returns l if list $1 is longer than list $2, g if list $1 is shorter than list $2, empty token if lists have equal length
override words_cmp = $(if $(call nonequal,$(words $1),$(words $2)),$(if $(wordlist $(patsubst 0,1,$(words $2)),$(words $1),$1),g,l))

# Returns l if digit $1 is less than digit $2, g if $1 is greater than $2, empty token if equal
override dig_cmp = $(if $(call nonequal,$1,$2),$(if $(filter $1,$(lastword $(sort $1 $2))),g,l))

# Insert spaces around every digit
override dig_split = $(strip $(subst 0, 0 ,$(subst 1, 1 ,$(subst 2, 2 ,$(subst 3, 3 ,$(subst 4, 4 ,$(subst 5, 5 ,$(subst 6, 6 ,$(subst 7, 7 ,$(subst 8, 8 ,$(subst 9, 9 ,$(firstword $1 0))))))))))))

# Split number into list of digits, returns 0 for non-numbers or empty values
override num_split = $(filter 0 1 2 3 4 5 6 7 8 9,$(call dig_split,$1))

# Returns l if number $1 is less than number $2, g if $1 is greater than $2, empty token if equal
override num_cmp = $(strip $(call words_cmp,$(call num_split,$1),$(call num_split,$2)) $(foreach pair,$(join $(call num_split,$1),$(patsubst %,$(MAGIC)%,$(call num_split,$2))),$(call dig_cmp,$(word 1,$(subst $(MAGIC), ,$(pair))),$(word 2,$(subst $(MAGIC), ,$(pair))))))

# Split version into a list (major minor patch ...)
override ver_split = $(strip $(subst ., ,$(subst -, ,$1.0.0.0.0)))

# Returns l if version $1 is lower than version $2, g if version $1 is greater than version $2, empty token if equal
override ver_cmp = $(firstword $(foreach pair,$(join $(call ver_split,$1),$(patsubst %,$(MAGIC)%,$(call ver_split,$2))),$(call num_cmp,$(word 1,$(subst $(MAGIC), ,$(pair))),$(word 2,$(subst $(MAGIC), ,$(pair))))))

# Checks whether semantic version $1 is greater or equal than semantic version $2, returns it if so
override ver_check = $(if $(filter l,$(call ver_cmp,$1,$2)),,$1)

# Checks whether the running GNU Make version is greater or equal to passed
override make_min_ver = $(call ver_check,$(MAKE_VERSION),$1)

# Remove excess slashes in path
override path_strip = $(subst //,/,$(subst //,/,$(subst //,/,$(subst //,/,$1))))

# Escape internal path list for shell
override path_shell = $(subst ?,\$(SPACE),$1)

# Convert shell-escaped path list to an internal path representation suitable for GNU Make
override path_make = $(subst \$(SPACE),?,$1)

# Convert a single path variable (Possibly containing spaces) to an internal path representation
override path_wrap = $(subst $(SPACE),?,$(call path_make,$1))

# Escape string variable to an internal representation suitable for shell_ex prinf invocation
override str_wrap = $(QUOT)$(subst -,$(BSLSH)055,$(subst $(SPACE),$(MAGIC),$(subst $(QUOT),\$(QUOT),$(subst $(NEWLINE),\n,$(subst \,\\,$1)))$(QUOT)))

# Escape shell invocation containing internal path/string representations
override shell_esc = $(foreach part,$1,$(if $(filter "%",$(part)),$(subst $(MAGIC),$(SPACE),$(part)),$(subst /,$(if $(HOST_POSIX),/,$(BSLSH)),$(subst $(QUOT),\$(QUOT),$(call path_shell,$(part))))))

# Invoke shell command with escaping
override shell_ex = $(shell $(call shell_esc,$1))

# Space-aware wildcard
override safe_wildcard = $(foreach wc,$(call path_strip,$(call path_make,$1)),$(foreach lp,$(firstword $(subst *,$(SPACE),$(wc))),$(subst ?$(lp),$(SPACE)$(lp),$(call path_wrap,$(wildcard $(call path_shell,$(wc)))))))

# Returns paths in list which are missing in the filesystem
# NOTE: Older GNU Make caches directory contents and ignores files created in current run
override paths_missing = $(filter-out $(call safe_wildcard,$1),$1)

# Check whether all paths in list exist in the filesystem, returns 1 if so
# NOTE: Older GNU Make caches directory contents and ignores files created in current run
override paths_exist = $(if $(call paths_missing,$1),,1)

# Check if path is a directory, returns 1 if so
override is_dir = $(if $(call equal,$1/,$(wildcard $(call path_strip,$(call path_wrap,$1/)))),1)

# List directory contents
override ls_dir = $(if $1,$(call safe_wildcard,$(call path_wrap,$1)/*))

# List directories inside a directory
override next_dirs = $(if $1,$(call safe_wildcard,$(call path_wrap,$1)/*/))

# Recusively list files in a directory (Space-aware, replaces spaces with ?)
# NOTE: May only handle up to 8 levels of nesting, because GNU Make 3.7x doesn't support actuall recursive $(call)
override tree_dir = $(strip $(call ls_dir,$1) $(foreach l1,$(call next_dirs,$1),$(call ls_dir,$(l1)) $(foreach l2,$(call next_dirs,$(l1)),$(call ls_dir,$(l2)) $(foreach l3,$(call next_dirs,$(l2)),$(call ls_dir,$(l3)) $(foreach l4,$(call next_dirs,$(l3)),$(call ls_dir,$(l4)) $(foreach l5,$(call next_dirs,$(l4)),$(call ls_dir,$(l5)) $(foreach l6,$(call next_dirs,$(l5)),$(call ls_dir,$(l6)) $(foreach l7,$(call next_dirs,$(l6)),$(call ls_dir,$(l7))))))))))

# Recusive filesystem match of $2 in dir $1
override recursive_match = $(filter $(subst *,%,$2),$(call tree_dir,$1))

# Recusive filesystem wildcard
override recursive_wildcard = $(foreach wc,$1,$(call recursive_match,$(dir $(wc)),$(wc)))

# Create full directory paths, i.e. portable mkdir -p $1
override create_dirs = $(if $(if $(call paths_missing,$1),$(if $(HOST_POSIX),$(call shell_ex,mkdir -p $(call paths_missing,$1)),$(call shell_ex,md $(call paths_missing,$1) 2>&1))),)

# Install file $1 at path $2 under permissions $3
override install_file = $(if $(foreach dst,$(call path_wrap,$2),$(foreach src,$(call path_wrap,$1),$(call create_dirs,$(dir $(dst)))$(if $(call shell_ex,install -m $(firstword $3 0644) $(src) $(dst) 2>&1),$(call shell_ex,$(if $(HOST_POSIX),cp,copy) $(src) $(dst) 2>&1)))),)

# Install string $1 as file $2
override install_string = $(if $(foreach dst,$(call path_wrap,$2),$(call create_dirs,$(dir $(dst)))$(if $(HOST_POSIX),$(call shell_ex,printf $(call str_wrap,$1$(NEWLINE)) >$(dst)),$(file >$(dst),$1))),)

# Canonize architecture from triplet / uname (Also lower-case it)
# amd64, x64, em64t, i86*  -> x86_64 (i86 is a Solaris gimmick)
# *86                      -> i386
# aarch64*, armv8*, armv9* -> arm64
# aarch64_be*              -> arm64be
# arm*, thumbv*            -> arm
# armeb*                   -> armeb
# mipsisa64*               -> mips64
# mipsisa32*               -> mips
# riscv, riscv64*          -> riscv64
# riscv32*                 -> riscv32
# x86_64                   -> i386 (If -m32 is passed in CFLAGS)
override canonize_arch = $(patsubst !%,%,$(patsubst x86_64,$(if $(filter -m32,$(CFLAGS)),i386,x86_64),$(patsubst riscv,riscv64,$(patsubst riscv64%,riscv64,$(patsubst riscv32%,riscv32,$(patsubst mipsisa64%,mips64,$(patsubst mipsisa32%,mips,$(patsubst thumbv%,arm,$(patsubst arm%,arm,$(patsubst armeb%,!armeb,$(patsubst arm64%,!arm64,$(patsubst armv8%,arm64,$(patsubst armv9%,arm64,$(patsubst aarch64%,arm64,$(patsubst aarch64_be%,!arm64be,$(patsubst x86_64%,x86_64,$(patsubst i86%,x86_64,$(patsubst %86,i386,$(patsubst em64t,x86_64,$(patsubst amd64,x86_64,$(patsubst x64,x86_64,$(call tolower,$1))))))))))))))))))))))

# Canonize OS from triplet / uname (Also lower-case it and remove trailing OS version)
# mingw*   -> windows
# macos*   -> darwin
# solaris* -> sunos
override canonize_os = $(patsubst msdos%,dos,$(patsubst solaris%,sunos,$(patsubst macos%,darwin,$(patsubst mingw%,windows,$(call tolower,$(if $(findstring .,$1),$(firstword $(call dig_split,$1)),$1))))))

# Canonize compiler brand
# oneapi, llvm -> clang
# cosmocc      -> gcc
override canonize_cc = $(patsubst cosmocc,gcc,$(patsubst oneapi,clang,$(patsubst llvm,clang,$(call tolower,$1))))

# Prettify OS name for printing (Also capitalizes the output)
# *bsd  -> *BSD
# *os   -> *OS
# gnu   -> GNU
# dos   -> DOS
# cosmo -> Cosmopolitan
override prettify_os = $(call capitalize,$(patsubst cosmo,Cosmopolitan,$(patsubst gnu,GNU,$(patsubst %os,%OS,$(patsubst dos,DOS,$(patsubst %bsd,%BSD,$1))))))

# Prettify compiler name (Also capitalizes the output)
# *cc    -> *CC (GCC, TCC)
# clang  -> LLVM Clang
override prettify_cc = $(call capitalize,$(patsubst clang,LLVM Clang,$(patsubst %cc,%CC,$1)))

# Check whether source $1 compiles with flags $2, cache result as $3
# NOTE: May only be called after "Project output handling" stage
override check_compile = $(foreach out,$(call path_wrap,$(firstword $(OBJDIR) .)/check-$(subst =,,$(subst $(COMMA),,$(subst $(SPACE),-,$(strip $(subst -,$(SPACE),$3)))))),$(if $(call paths_exist,$(out)$(BIN_EXT)),1,$(call install_string,$1,$(out).c)$(if $(call shell_ex,$(CC) $2 $(CFLAGS) $(LDFLAGS) -w $(out).c -o $(out)$(BIN_EXT) 2>&1),,1)))

# Check CFLAG/LDFLAG availability (I.e. whether a library may be linked)
override check_cc_flag = $(if $1,$(if $(call check_compile,int main(){return 0;},$1,$1),$1))
override check_cc_flags = $(if $(call check_cc_flag,$1),$1,$(foreach ldflag,$1,$(call check_cc_flag,$(ldflag))))

###################################################################################################
#
# Determine build host features, shell / logger helpers
#
###################################################################################################

# Clean up garbage OS env passed on Windows
override OS := $(filter-out Windows_NT,$(OS))

# Get host uname; pipe stderr in a portable way. Don't use uname output if it's not exactly one word (Likely due to command failure).
# NOTE: Cygwin and MSYS return garbage in `uname -s`, `uname -o` gives a prettier result.
# NOTE: We might also get "MS/Windows" or "Windows_NT" from uname on e.q. w64devkit.
# NOTE: MSYS, w64devkit, etc are considered POSIX-like with forward slashes; MinGW with stock Windows CMD is not
override HOST_UNAME := $(call var_single,$(shell uname -s 2>&1))
override HOST_UNAME := $(firstword $(if $(call find_any_str,- _ .,$(HOST_UNAME)),$(call var_single,$(shell uname -o 2>&1))) $(HOST_UNAME))
override HOST_POSIX := $(firstword $(findstring Windows,$(HOST_UNAME)) $(HOST_UNAME) $(if $(call var_def,windir)$(call var_def,WINDIR),,POSIX))
override HOST_UNAME := $(firstword $(HOST_POSIX) Windows)

# Canonize host OS, determine host architecture, where to pipe null & number of cores
override HOST_OS   := $(call canonize_os,$(HOST_UNAME))
override HOST_ARCH := $(call canonize_arch,$(firstword $(if $(filter windows,$(HOST_OS)),$(PROCESSOR_ARCHITECTURE),$(shell uname -m 2>/dev/null)) Unknown))
override HOST_NULL := $(if $(HOST_POSIX),/dev/null,nul)
override HOST_CPUS := $(firstword $(if $(filter windows,$(HOST_OS)),$(NUMBER_OF_PROCESSORS),$(shell getconf _NPROCESSORS_ONLN 2>/dev/null || nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null)) 1)

# Helpers for piping $(shell) invocation output (stdout/stderr)
override NULL_STDOUT := 1>$(HOST_NULL)
override NULL_STDERR := 2>$(HOST_NULL)
override PIPE_STDERR := 2>&1
override ONLY_STDERR := $(PIPE_STDERR) $(NULL_STDOUT)

# Print function
ifneq (,$(call make_min_ver,3.81))
# Use $(info $1) on GNU Make 3.81+ (See GNU Make NEWS file)
override println = $(if $(info $1),)
else
# Use $(call shell_ex,printf $(call str_wrap,$1$(NEWLINE)) 1>&2) on legacy GNU Make
override println = $(if $(call shell_ex,printf $(call str_wrap,$1$(NEWLINE)) 1>&2),)
endif

# Colorful logging attributes
override VT_ESC  := $(if $(if $(call make_min_ver,4.1),$(call var_def,MAKE_TERMOUT),stdout)$(filter true,$(call var_def,CI)),)
override VT_BELL := $(if $(VT_ESC),)
override RESET   := $(if $(VT_ESC),$(VT_ESC)[0m)
override BOLD    := $(if $(VT_ESC),$(VT_ESC)[1m)
override RED     := $(if $(VT_ESC),$(VT_ESC)[31m)
override GREEN   := $(if $(VT_ESC),$(VT_ESC)[32m)
override YELLOW  := $(if $(VT_ESC),$(VT_ESC)[33m)
override WHITE   := $(if $(VT_ESC),$(VT_ESC)[37m)
override BLUE    := $(if $(VT_ESC),$(VT_ESC)[34m$(VT_ESC)[38;5;38m)
override ORANGE  := $(if $(VT_ESC),$(VT_ESC)[33m$(VT_ESC)[38;5;202m)
override TEXT    := $(if $(VT_ESC),$(VT_ESC)[0;1m)

# Logger prefixes
override INFO_PREFIX := $(TEXT)[$(YELLOW)INFO$(TEXT)]
override WARN_PREFIX := $(TEXT)[$(RED)WARN$(TEXT)]

# Logger functions
override log_info = $(call println,$(INFO_PREFIX) $1 $(RESET))
override log_warn = $(call println,$(WARN_PREFIX) $1 $(RESET))

# Automatically parallelize build to host cores
JOBS ?= $(HOST_CPUS)
override MAKEFLAGS += -j $(JOBS)

###################################################################################################
#
# Determine build target features for proper cross-compilation
#
###################################################################################################

# Get compiler target triplet in the form of arch-[vendor-]os[-abi]
override CC_TRIPLET := $(call var_single,$(call shell_ex,$(CC) $(CFLAGS) -dumpmachine $(NULL_STDERR)))

# Manually provide triplet for cosmocc (Note that in fact it's both x86_64 and arm64)
override TRIPLET := $(firstword $(if $(findstring -,$(CC_TRIPLET)),$(CC_TRIPLET)) $(if $(findstring cosmocc,$(CC)),x86_64-cosmo))

override TRIPLET_WORDS := $(subst -,$(SPACE),$(TRIPLET))
override TRIPLET_ARCH  := $(word 1,$(TRIPLET_WORDS))
override TRIPLET_2     := $(word 2,$(TRIPLET_WORDS))
override TRIPLET_3     := $(word 3,$(TRIPLET_WORDS))
override TRIPLET_4     := $(word 4,$(TRIPLET_WORDS))

# Assume arch-vendor-os[-abi] triplet form by default
override TRIPLET_OS := $(TRIPLET_3)
ifeq (,$(TRIPLET_4))
ifeq (,$(TRIPLET_3))
# This is a pure arch-os triplet form
override TRIPLET_OS := $(TRIPLET_2)
else
ifneq (,$(if $(filter-out $(TRIPLET_KNOWN_VENDORS),$(TRIPLET_2)),$(call find_any_str,$(TRIPLET_KNOWN_ABI_STR),$(TRIPLET_3))))
# This is an ambiguous arch-os-abi form. The second tuple isn't a known vendor, and the third tuple contains known ABI string.
override TRIPLET_OS := $(TRIPLET_2)
endif
endif
endif

# Handle special cases where OS name might be part of ABI tuple
override TRIPLET_OS := $(firstword $(call find_any_str,$(TRIPLET_KNOWN_OS_PRIO),$(TRIPLET)) $(filter-out elf,$(TRIPLET_OS)))

# Warn when neither compiler triplet nor user supplied enough cross-compile info
ifeq (,$(filter cc,$(CC))$(TRIPLET_OS)$(OS)$(TRIPLET_ARCH)$(ARCH))
$(call log_info,Assuming target $(HOST_ARCH)-$(HOST_OS). Set OS/ARCH manually if cross-compiling.)
endif

# Canonize OS/ARCH, prioritize: compiler triplet > user-supplied > host
override OS      := $(call canonize_os,$(firstword $(TRIPLET_OS) $(OS) $(if $(filter ia16,$(TRIPLET_ARCH)),dos) $(HOST_OS)))
override ARCH    := $(call canonize_arch,$(firstword $(TRIPLET_ARCH) $(ARCH) $(HOST_ARCH)))
override TRIPLET := $(ARCH)-$(OS)

# For cross-compile checking (Cosmopolitan is treated as non-cross target)
override TARGET_CROSS := $(if $(filter-out cosmo,$(OS)),$(if $(filter-out $(ARCH),$(HOST_ARCH))$(filter-out $(OS),$(HOST_OS)),$(ARCH)-$(OS)))

# Pretty OS name for printing (Taken from uname if target matches the host)
override OS_PRETTY := $(if $(filter $(OS),$(HOST_OS)),$(HOST_UNAME),$(call prettify_os,$(OS)))

###################################################################################################
#
# Determine compiler brand & version
#
###################################################################################################

# Search for "<cc_brand> version N[.x.y]" pattern in $(cc -v) dump (Works for GCC, Clang, TCC)
override CC_VERSION_DUMP    := $(call shell_ex,$(CC) -v $(PIPE_STDERR))
override CC_VERSION_TRIPLET := $(foreach tmp,$(subst $(SPACE)version$(SPACE),$(MAGIC)version$(MAGIC),$(strip $(CC_VERSION_DUMP))),$(if $(findstring $(MAGIC)version$(MAGIC),$(tmp)),$(tmp)))
override CC_VERSION_TRIPLET := $(strip $(foreach tmp,$(CC_VERSION_TRIPLET),$(if $(call is_numeric,$(word 3,$(subst $(MAGIC),$(SPACE),$(tmp)))),$(subst $(MAGIC),$(SPACE),$(tmp)))))

# If compiler version triplet is empty & CC_BRAND isn't provided, fallback to heuristics
override CC_BRAND := $(call canonize_cc,$(firstword $(word 1,$(CC_VERSION_TRIPLET)) $(CC_BRAND) $(filter $(CC_KNOWN_BRANDS),$(CC_VERSION_DUMP)) $(lastword $(subst -, ,$(notdir $(CC))))))

# If neither compiler version triplet nor $(cc -dumpfullversion -dumpversion) provide a proper version, fallback to user-supplied CC_VERSION or whatever we've got
override CC_VERSION := $(firstword $(call is_semver,$(word 3,$(CC_VERSION_TRIPLET))) $(call is_semver,$(call shell_ex,$(CC) -dumpfullversion -dumpversion $(NULL_STDERR))) $(CC_VERSION) $(word 3,$(CC_VERSION_TRIPLET)))

ifeq ($(ARCH),e2k)
# LCC is not a real GCC, but lies about being one
# Workaround build failures by explicitly marking it as different compiler brand
override CC_BRAND := ПТН ПНХ
endif

# Check whether this compiler supports standard GNU extensions
override CC_IS_GCC   := $(if $(filter gcc,$(CC_BRAND)),1)
override CC_IS_CLANG := $(if $(filter clang,$(CC_BRAND)),1)
override CC_IS_GNU   := $(firstword $(CC_IS_GCC) $(CC_IS_CLANG))

# Compiler version checks
override gcc_min_ver   = $(if $(filter gcc,$(CC_BRAND)),$(call ver_check,$(CC_VERSION),$1))
override clang_min_ver = $(if $(filter clang,$(CC_BRAND)),$(call ver_check,$(CC_VERSION),$1))
override gnuc_min_ver  = $(if $(filter gcc clang,$(CC_BRAND)),$(call ver_check,$(CC_VERSION),$1))

# Pretty compiler info for printing
override CC_PRETTY := $(call prettify_cc,$(CC_BRAND)) $(CC_VERSION)

###################################################################################################
#
# Determine project tag, revision, commit id via git
#
###################################################################################################

override GIT_DESCRIBE := $(firstword $(call var_single,$(call shell_ex,git describe --tags --always --long --dirty $(PIPE_STDERR))) $(call var_def,GIT_DESCRIBE) $(call var_def,GIT_COMMIT))
override GIT_DIRTY    := $(if $(filter %-dirty,$(GIT_DESCRIBE)),dirty)
override _            := $(patsubst %-dirty,%,$(GIT_DESCRIBE))
override GIT_COMMIT   := $(lastword $(subst -g,$(SPACE),$(_)))
override _            := $(filter-out $(GIT_COMMIT),$(patsubst %-g$(GIT_COMMIT),%,$(_)))
override GIT_REVISION := $(call is_numeric,$(lastword $(subst -,$(SPACE),$(_))))
override GIT_TAG      := $(patsubst %-$(GIT_REVISION),%,$(_))

# Produce a git version in the form of tag-g1234567-dirty for dev builds, tag for releases,
# and just a commit hash for non-tagged projects. Takes a fallback tag (For shallow clones).
override git_version = $(firstword $(patsubst %,%$(if $(filter 0,$(GIT_REVISION)$(GIT_DIRTY)),,$(patsubst %,-g%,$(GIT_COMMIT))),$(firstword $(GIT_TAG) $1)) $(GIT_COMMIT))$(patsubst %,-%,$(GIT_DIRTY))

###################################################################################################
#
# Set up target-specific build options (File extensions, low-level platform libs, pkg-config, etc)
#
###################################################################################################

# POSIX: Use .so lib extension
override BIN_EXT :=
override LIB_EXT := .so

# Windows: Use .exe/.dll extensions, link statically
ifeq ($(OS),windows)
override BIN_EXT := .exe
override LIB_EXT := .dll
override LDFLAGS := $(LDFLAGS) -static -Wl$(COMMA)--subsystem$(COMMA)console:3.10 -Wl$(COMMA)--exclude-all-symbols
endif

# MacOS: Use .dylib lib extension
ifeq ($(OS),darwin)
override LIB_EXT := .dylib
endif

# SunOS (Solaris, Illumos): LTO is broken, duh
ifeq ($(OS),sunos)
USE_LTO ?= 0
endif

# Cosmopolitan, Redox have no shared library support
ifneq (,$(filter cosmo redox,$(OS)))
USE_LIB ?= 0
endif

# DOS: Use .com bin extension, various quirks
ifeq ($(OS),dos)
override BIN_EXT := .com
override CFLAGS  := $(CFLAGS) -Os -Wno-format -Wno-attributes
ifeq ($(ARCH),i386)
override CFLAGS := $(CFLAGS) -march=i386
endif
ifeq ($(ARCH),ia16)
override CFLAGS := $(CFLAGS) -mcmodel=medium -D__SIZE_TYPE__=__UINT32_TYPE__ -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast
endif
USE_LIB ?= 0
USE_NET ?= 0
endif

# AmigaOS: Disable USE_LTO, USE_LIB, USE_NET
ifeq ($(OS),amigaos)
override LIB_EXT := .library
override CFLAGS  := $(CFLAGS) -noixemul
USE_LTO ?= 0
USE_LIB ?= 0
USE_NET ?= 0
endif

# Emscripten: Use .mjs extension, disable shared library support
# Disable setjmp & implicit traps for optimization, enable pthreads + main thread proxying, allow memory growth, enable full filesystem & fetch API
ifeq ($(OS),emscripten)
override BIN_EXT := .mjs
override CFLAGS  := -pthread -s SUPPORT_LONGJMP=0 $(CFLAGS)
override LDFLAGS := -s DEFAULT_TO_CXX=0 -s BINARYEN_IGNORE_IMPLICIT_TRAPS=1 -s ALLOW_MEMORY_GROWTH=1 \
-s MEMORY_GROWTH_LINEAR_STEP=1mb -s PROXY_TO_PTHREAD=1 -s FORCE_FILESYSTEM=1 -s FETCH=1 $(LDFLAGS)
# Disable shared library & network by default
USE_LIB ?= 0
USE_NET ?= 0
endif

# Disable pkg-config if it's a cross-compile target and neither PKG_CONFIG_LIBDIR or PKG_CONFIG_SYSROOT_DIR are set, pass -static if needed
override PKG_CONFIG := $(if $(if $(TARGET_CROSS),$(call var_def,PKG_CONFIG_LIBDIR)$(call var_def,PKG_CONFIG_SYSROOT_DIR)$(filter-out pkg-config,$(PKG_CONFIG)),1),$(PKG_CONFIG) $(filter -static,$(LDFLAGS)))

###################################################################################################
#
# Project outputs handling, any of these may be overriden by project spec
#
###################################################################################################

# Decide BUILDDIR / OBJDIR / SRCDIR
override BUILD_TYPE := $(if $(call var_use,USE_DEBUG_FULL),debug,release)
override BUILDDIR   ?= $(BUILD_TYPE).$(OS).$(ARCH)
override SRCDIR     := src
override INCDIR     := include
override OBJDIR     := $(BUILDDIR)/obj

# Convert base target name into target filename (For target binary invocation, etc)
override bin_target        = $(patsubst %,$(BUILDDIR)/%_$(ARCH)$(BIN_EXT),$1)
override lib_target        = $(patsubst %,$(BUILDDIR)/lib%$(LIB_EXT),$1)
override lib_static_target = $(patsubst %,$(BUILDDIR)/lib%_static.a,$1)

# Covert target filenames to base target names
override bin_base        = $(patsubst $(call bin_target,%),%,$1)
override lib_base        = $(patsubst $(call lib_target,%),%,$1)
override lib_static_base = $(patsubst $(call lib_static_target,%),%,$1)

# Get list of sources from target base, uses a `BIN_SRC_$(name)` list by default
override bin_base_src = $(call var_src,bin_src_$1)
override lib_base_src = $(call var_src,lib_src_$1)

# Get list of libraries from target base, expects a `BIN_LIBS_$(name)` list by default
override bin_base_libs = $(call var_src,bin_libs_$1)
override lib_base_libs = $(call var_src,lib_libs_$1)

###################################################################################################
#
# Project specification
#
# Supported variables (Useflags are controllable from env):
# - NAME:    Project name, used for install paths, pkg-config; Passed as -DPROJECT_VERSION=...
# - DESC:    Project description, used for pkg-config generation
# - LOGO:    Project ASCII logo (If any), shown in build splash
# - URL:     Project homepage URL
# - VERSION: Project version (Fallback if git describe fails, useful for release source archives)
#
# - SRCDIR:   Project source directory, defaults to ./src
# - INCDIR:   Project public headers directory, defaults to ./include
# - BUILDDIR: Build directory, defaults to $(BUILD_TYPE).$(OS).$(ARCH), for example release.linux.i386
# - OBJDIR:   Object files directory, defaults to $(BUILDDIR)/obj
#
# - BIN_TARGETS: List of binary targets to build
# - LIB_TARGETS: List of library targets to build
#
# - BIN_SRC_$(bin): List of source files to build a respective binary target
# - LIB_SRC_$(lib): List of source files to build a respective library target
#
# - BIN_LIBS_$(bin): List of pkg-config / project libraries to link into the respective binary target
# - LIB_LIBS_$(lib): List of pkg-config / project libraries to link into the respective library target
#
# - USE_*:         Useflag, passed as -DUSE_FLAG=$(USE_FLAG) if set to non-zero value, controls build features
# - DEPS_USE_*:    List of flags on which respective USE_FLAG flag depends, which should have a non-zero value
# - IMPLY_USE_*:   List of useflags which are auto-enabled when a respective USE_FLAG is enabled
# - SRC_USE_*:     Sources which should be built ONLY when a respective USE_FLAG is enabled
# - CFLAGS_USE_*:  CFLAGS to be passed when a respective USE_FLAG is enabled
# - LDFLAGS_USE_*: LDFLAGS to be passed when a respective USE_FLAG is enabled
# - LIBS_USE_*:    List of pkg-config/project libraries to link ONLY when a respective USE_FLAG is enabled
#
###################################################################################################

include project.mk

# Validate project spec variables
override NAME     := $(firstword $(call var_src,NAME) Project)
override DESC     := $(call var_src,DESC)
override LOGO     := $(call var_src,LOGO)
override URL      := $(call var_src,URL)
override VERSION  := $(call git_version,$(call var_src,VERSION))
override SRCDIR   := $(call path_wrap,$(SRCDIR))
override INCDIR   := $(call safe_wildcard,$(call path_wrap,$(INCDIR)))
override BUILDDIR := $(call path_wrap,$(BUILDDIR))
override OBJDIR   := $(call path_wrap,$(OBJDIR))

override NAME_LOWER := $(call tolower,$(subst $(SPACE),-,$(NAME)))
override NAME_UPPER := $(call toupper,$(subst -,_,$(NAME_LOWER)))

override BIN_BASE_LIST := $(call bin_base,$(call path_make,$(call var_src,BIN_TARGETS)))
override LIB_BASE_LIST := $(call lib_base,$(call path_make,$(call var_src,LIB_TARGETS)))

override BIN_TARGETS        := $(call bin_target,$(BIN_BASE_LIST))
override LIB_TARGETS        := $(call lib_target,$(LIB_BASE_LIST))
override LIB_STATIC_TARGETS := $(call lib_static_target,$(LIB_BASE_LIST))

###################################################################################################
#
# Common build configuration
#
###################################################################################################

# Compile options
USE_LTO         ?= 1          # Use Link-Time Optimizations
USE_LIB         ?= 1          # Build shared libraries
USE_LIB_STATIC  ?= 0          # Build static libraries
USE_LIB_SHARING ?= $(USE_LIB) # Link to shared project libraries
USE_OBJ_STAGE   ?= 1          # Use intermediate object file build step
USE_DEBUG_FULL  ?= 0          # Full debug build without optimizations

# Warning / Static analysis / Sanitizer options
USE_WARNS    ?= 4 # Warning level
USE_ANALYZER ?= 0 # Use Clang Static Analyzer / GCC static analyzer
USE_UBSAN    ?= 0 # Use UndefinedBehaviorSanitizer
USE_ASAN     ?= 0 # Use AddressSanitizer
USE_TSAN     ?= 0 # Use ThreadSanitizer
USE_MSAN     ?= 0 # Use MemorySanitizer

# Optimized build with debug info
USE_DEBUG ?= $(firstword $(filter-out 0,$(USE_DEBUG_FULL) $(USE_UBSAN) $(USE_ASAN) $(USE_TSAN) $(USE_MSAN)) 0)

# Feature options (Enable USE_NET if you wish to use sockets, etc)
USE_FPU ?= 1
USE_NET ?= 0
USE_SDL ?= 0

override DEPS_USE_LTO         := CC_IS_GNU
override DEPS_USE_LIB         := CC_IS_GNU
override DEPS_USE_LIB_STATIC  := USE_OBJ_STAGE
override DEPS_USE_LIB_SHARING := USE_LIB

override DEPS_USE_UBSAN := CC_IS_GNU
override DEPS_USE_ASAN  := CC_IS_GNU
override DEPS_USE_TSAN  := CC_IS_GNU
override DEPS_USE_MSAN  := CC_IS_CLANG

override CFLAGS_USE_DEBUG := -DDEBUG -g -fno-omit-frame-pointer
override CFLAGS_USE_LIB   := $(if $(filter-out windows amigaos,$(OS)),-fPIC)
override CFLAGS_USE_UBSAN := -fsanitize=undefined,float-cast-overflow -fsanitize-recover=undefined,float-cast-overflow
override CFLAGS_USE_ASAN  := -fsanitize=address -fsanitize-recover=address
override CFLAGS_USE_TSAN  := -fsanitize=thread -Wno-tsan
override CFLAGS_USE_MSAN  := -fsanitize=memory -fsanitize-recover=memory

override LDFLAGS_USE_FPU := -lm

override LIBS_USE_SDL := sdl$(filter-out 1,$(USE_SDL))

ifeq ($(OS),windows)
# Windows-specific libraries to link to
# NOTE: Prefer linking to wsock32 on i386 for Win95/NT 3.x compat, this still allows to use WinSock 2.x when available
override LDFLAGS_USE_WIN32_GUI := $(call check_cc_flags,-lgdi32)
override LDFLAGS_USE_NET       := $(call check_cc_flags,$(if $(filter i386,$(ARCH)),-lwsock32,-lws2_32) -lws2)
endif

ifeq ($(OS),haiku)
# Haiku-specific libraries to link to
override LDFLAGS_USE_HAIKU_GUI := -lbe
override LDFLAGS_USE_NET       := -lnetwork
endif

ifeq ($(OS),darwin)
# macOS-specific frameworks to link to
override LDFLAGS_USE_COCOA_GUI := -framework Cocoa
endif

ifeq ($(OS),sunos)
# Solaris-specific libraries to link to
override LDFLAGS_USE_NET := $(call check_cc_flags,-lsocket)
endif

ifeq ($(OS),emscripten)
# Request SDL port to be enabled in Emscripten
override CFLAGS_USE_SDL := -s USE_SDL=$(USE_SDL)
endif

# POSIX: Link to libpthread, librt, libdl when available
# Link to libatomic when available
override LDFLAGS := $(LDFLAGS) $(call check_cc_flags,$(if $(filter-out windows,$(OS)),-lpthread -lrt -ldl) -latomic)

# Report possibly missing libatomic
ifeq (,$(filter -latomic,$(LDFLAGS)))
override USE_NO_LIBATOMIC ?= 1
endif

###################################################################################################
#
# Useflags automation magic
#
###################################################################################################

ifndef .VARIABLES
# Backward compatibility with ancient GNU Make (<3.80) where .VARIABLES is not even supported yet
# Dump all words in Makefile, project.mk and env, then filter actual variables via $(origin)
override .VARIABLES := $(sort $(foreach w,$(shell cat Makefile) $(shell cat project.mk) $(shell printenv | sed 's;=.*;;'),$(if $(filter undefined,$(origin $(w))),,$(w))))
endif

override USEFLAGS  := $(sort $(filter USE_%,$(.VARIABLES)))
override SRC_COND  := $(sort $(filter SRC_USE_%,$(.VARIABLES)))
override LIBS_COND := $(sort $(filter LIBS_USE_%,$(.VARIABLES)))

override useflag_filter_deps = $(foreach useflag,$(USEFLAGS_ON),$(if $(strip $(foreach dep,$(call var_src,DEPS_$(useflag)),$(if $(call var_use,$(filter-out $(filter-out $(USEFLAGS_ON),$(USEFLAGS)),$(dep))),,$(dep)$(call log_warn,$(useflag) depends on $(dep))))),,$(useflag)))

# List enabled and implied useflags which have all of their dependencies satisfied
override USEFLAGS_ON := $(foreach useflag,$(USEFLAGS),$(if $(call var_use,$(useflag)),$(useflag)))
override USEFLAGS_ON := $(call useflag_filter_deps)
override USEFLAGS_ON := $(call useflag_filter_deps)
override USEFLAGS_ON := $(strip $(USEFLAGS_ON) $(filter-out $(USEFLAGS_ON),$(sort $(foreach useflag,$(USEFLAGS_ON),$(call var_src,IMPLY_$(useflag))))))
override USEFLAGS_ON := $(call useflag_filter_deps)
override USEFLAGS_ON := $(call useflag_filter_deps)

# List disabled useflags
override USEFLAGS_OFF := $(filter-out $(USEFLAGS_ON),$(USEFLAGS))

# List conditionally disabled sources to filter them out later
override SRC_OFF := $(sort $(foreach src,$(SRC_COND),$(if $(filter $(patsubst SRC_USE_%,USE_%,$(src)),$(USEFLAGS_ON)),,$(call var_src,$(src)))))

# List all possible library names
override LIBS_ALL := $(sort $(foreach lib,$(LIBS_COND),$(call var_src,$(lib))) $(foreach bin,$(BIN_BASE_LIST),$(call bin_base_libs,$(bin))) $(foreach lib,$(LIB_BASE_LIST),$(call lib_base_libs,$(lib))))

# List conditionally disabled libraries to filter them out later
override LIBS_OFF := $(sort $(foreach lib,$(LIBS_COND),$(if $(filter $(patsubst LIBS_USE_%,USE_%,$(lib)),$(USEFLAGS_ON)),,$(call var_src,$(lib)))))

# List all enabled libraries
override LIBS_ON := $(filter-out $(LIBS_OFF),$(LIBS_ALL))

# Set useflags -DUSE_* definitions
override CPPFLAGS := $(CPPFLAGS) $(foreach useflag,$(USEFLAGS_ON),-D$(useflag)=$(firstword $(call var_use,$(useflag)) 1))

# Set useflags CFLAGS
override CFLAGS := $(CFLAGS) $(foreach useflag,$(USEFLAGS_ON),$(call var_src,CFLAGS_$(useflag)))

# Set useflags LDFLAGS
override LDFLAGS := $(LDFLAGS) $(foreach useflag,$(USEFLAGS_ON),$(call var_src,LDFLAGS_$(useflag)))

# Handle library include paths
override LIBS_PKG := $(sort $(filter-out $(LIB_BASE_LIST),$(LIBS_ON)))
override LIBS_OUT := $(if $(LIBS_PKG),$(call shell_ex,$(PKG_CONFIG) $(LIBS_PKG) --cflags --libs $(NULL_STDERR)))
override LIBS_ERR := $(strip $(if $(LIBS_PKG),$(if $(LIBS_OUT),,$(foreach lib,$(LIBS_PKG),$(if $(call shell_ex,$(PKG_CONFIG) $(lib) --cflags --libs $(NULL_STDERR)),,$(lib))))))
override LIBS_OUT := $(strip $(if $(LIBS_PKG),$(if $(LIBS_OUT),$(LIBS_OUT),$(foreach lib,$(LIBS_PKG),$(call shell_ex,$(PKG_CONFIG) $(lib) --cflags --libs $(NULL_STDERR))))))
override CPPFLAGS := $(CPPFLAGS) $(patsubst -I%,$(if $(call gnuc_min_ver,3),-isystem%,-I%),$(filter-out $(filter-out -I% -isystem%,$(filter -%,$(LIBS_OUT))),$(LIBS_OUT)))

$(if $(LIBS_ERR),$(call log_warn,Missing pkg-config metadata for libraries: $(LIBS_ERR)))

###################################################################################################
#
# Target & object file handling
#
###################################################################################################

# Check whether any C++ sources are present in list
override src_has_cxx = $(filter %.cpp %.cxx %.cc,$1)

# Convert source paths to object file paths
override src_to_obj = $(if $(call var_use,USE_OBJ_STAGE),$(patsubst %.m,$(OBJDIR)/%.o,$(patsubst %.c,$(OBJDIR)/%.o,$(patsubst %.cpp,$(OBJDIR)/%.o,$(patsubst %.cxx,$(OBJDIR)/%.o,$(patsubst %.cc,$(OBJDIR)/%.o,$1))))))

# Get full source tree for list of libraries (For USE_LIB_SHARING), up to 8 levels of nesting is supported
override lib_walk_src = $(filter-out $(LIBS_ALL),$1) $(foreach lib,$(filter-out $(LIBS_OFF),$(filter $(LIB_BASE_LIST),$1)),$(filter-out $(SRC_OFF),$(call lib_base_src,$(lib))) $(call lib_base_libs,$(lib)))
override lib_tree_src = $(strip $(call lib_walk_src,$(call lib_walk_src,$(call lib_walk_src,$(call lib_walk_src,$(call lib_walk_src,$(call lib_walk_src,$(call lib_walk_src,$(call lib_walk_src,$1)))))))))

# Covert list of base library names to project-owned library targets (For USE_LIB_SHARING)
override libs_to_targets = $(if $(call var_use,USE_LIB_SHARING),$(strip $(foreach lib,$(filter-out $(LIBS_OFF),$(filter $(LIB_BASE_LIST),$1)),$(call lib_target,$(lib)))))

# Covert list of base library names to LDFLAGS provided by pkg-config
override libs_to_pkgconf = $(strip $(foreach lib,$(filter-out $(LIBS_OFF),$(filter-out $(LIB_BASE_LIST),$1)),$(call shell_ex,$(PKG_CONFIG) $(lib) --libs $(NULL_STDERR))))

# Get full tree of pkg-config LDFLAGS for list of libraries (For USE_LIB_SHARING), up to 8 levels of nesting is supported
override lib_walk_pkg = $(filter-out $(LIBS_ALL),$1) $(call libs_to_pkgconf,$1) $(foreach lib,$(filter-out $(LIBS_OFF),$(filter $(LIB_BASE_LIST),$1)),$(call lib_base_libs,$(lib)))
override lib_tree_pkg = $(strip $(call lib_walk_pkg,$(call lib_walk_pkg,$(call lib_walk_pkg,$(call lib_walk_pkg,$(call lib_walk_pkg,$(call lib_walk_pkg,$(call lib_walk_pkg,$(call lib_walk_pkg,$1)))))))))

# Covert list of base library names to LDFLAGS
override libs_to_ldflags = $(if $(call var_use,USE_LIB_SHARING),-L$(BUILDDIR) $(patsubst %,-l%,$(filter $(LIB_BASE_LIST),$1)) $(call libs_to_pkgconf,$1),$(call lib_tree_pkg,$1))

# Get list of base library names on which the target in question directly depends on
override bin_target_libs = $(strip $(filter-out $(LIBS_OFF),$(call bin_base_libs,$(call bin_base,$1))))
override lib_target_libs = $(strip $(filter-out $(LIBS_OFF),$(call lib_base_libs,$(call lib_base,$1))))

override bin_target_own_libs = $(call libs_to_targets,$(filter $(LIB_BASE_LIST),$(call bin_target_libs,$1)))
override lib_target_own_libs = $(call libs_to_targets,$(filter $(LIB_BASE_LIST),$(call lib_target_libs,$1)))

# Get list of sources for target
override bin_target_src = $(strip $(filter-out $(SRC_OFF),$(call bin_base_src,$(call bin_base,$1))) $(if $(call var_use,USE_LIB_SHARING),,$(call lib_tree_src,$(call bin_target_libs,$1))))
override lib_target_src = $(strip $(filter-out $(SRC_OFF),$(call lib_base_src,$(call lib_base,$1))) $(if $(call var_use,USE_LIB_SHARING),,$(call lib_tree_src,$(call lib_target_libs,$1))))
override lib_static_src = $(strip $(filter-out $(SRC_OFF),$(call lib_base_src,$(call lib_static_base,$1))))

# Get list of object files for target
override bin_target_obj = $(call src_to_obj,$(call bin_target_src,$1))
override lib_target_obj = $(call src_to_obj,$(call lib_target_src,$1))
override lib_static_obj = $(call src_to_obj,$(call lib_static_src,$1))

# Get final link list for target - Remove duplicate object/library files
override bin_target_link = $(strip $(sort $(if $(call var_use,USE_OBJ_STAGE),$(call bin_target_obj,$1),$(call bin_target_src,$1))) $(call libs_to_ldflags,$(call bin_target_libs,$1)))
override lib_target_link = $(strip $(sort $(if $(call var_use,USE_OBJ_STAGE),$(call lib_target_obj,$1),$(call lib_target_src,$1))) $(call libs_to_ldflags,$(call lib_target_libs,$1)))

# Get CC or CXX for binary/library linking based on presence of C++ code
override bin_target_ld = $(if $(if $(call var_use,USE_OBJ_STAGE),$(call src_has_cxx,$(call bin_target_src,$1))),$(CXX) $(CXX_STD),$(CC) $(CC_STD))
override lib_target_ld = $(if $(if $(call var_use,USE_OBJ_STAGE),$(call src_has_cxx,$(call lib_target_src,$1))),$(CXX) $(CXX_STD),$(CC) $(CC_STD))

# Generate lists of object files for binary and library dependencies - Do not sort
override BIN_OBJ := $(foreach bin,$(BIN_TARGETS),$(call bin_target_obj,$(bin)))
override LIB_OBJ := $(foreach lib,$(LIB_TARGETS),$(call lib_target_obj,$(lib)))

# Generate list of library targets for binary dependencies
override BIN_LIBS := $(sort $(foreach bin,$(BIN_TARGETS),$(call bin_target_own_libs,$(bin))))

override OBJ  := $(sort $(BIN_OBJ) $(LIB_OBJ))
override DEPS := $(patsubst %.o,%.d,$(OBJ))
override DIRS := $(sort $(BUILDDIR) $(OBJDIR) $(dir $(OBJ)))
override _    := $(call create_dirs,$(DIRS))

###################################################################################################
#
# Set up optimization/warning options
#
###################################################################################################

# Generic conservative build options
override OPTIMIZE_OPTS := $(if $(call var_use,USE_DEBUG_FULL),-O0,-O2)
override WARN_OPTS     :=
override CC_STD        := -std=c99
override CXX_STD       :=

# Check LTO support on GCC/Clang 5.0+ if USE_LTO is enabled
override LTO_SUPPORTED :=
ifneq (,$(if $(call var_use,USE_LTO),$(call gnuc_min_ver,5.0)))
override LTO_SUPPORTED := $(if $(call check_cc_flags,-flto),1,$(call log_info,LTO is not supported by this toolchain))
endif

# Enable basic warnings on GCC/Clang 3.0+ if USE_WARNS >= 1
ifneq (,$(if $(filter g,$(call num_cmp,$(call var_use,USE_WARNS),0)),$(call gnuc_min_ver,3.0)))
override WARN_OPTS := -Wall
endif

# Enable extra warnings on GCC/Clang 5.0+ if USE_WARNS >= 2
ifneq (,$(if $(filter g,$(call num_cmp,$(call var_use,USE_WARNS),1)),$(call gnuc_min_ver,5.0)))
override WARN_OPTS := $(WARN_OPTS) -Wextra -Wshadow -Werror=return-type
endif

# Enable strict safety warnings on GCC/Clang 7.0+ if USE_WARNS >= 3
# NOTE: -Wbad-function-cast, -Wconversion are counter-productive
ifneq (,$(if $(filter g,$(call num_cmp,$(call var_use,USE_WARNS),2)),$(call gnuc_min_ver,7.0)))
override WARN_OPTS := $(WARN_OPTS) -Wvla -Walloca -Wtrampolines -Wcast-qual -Wduplicated-cond
endif

# Enable strict portability warnings on GCC/Clang 8.0+ if USE_WARNS >= 4
ifneq (,$(if $(filter g,$(call num_cmp,$(call var_use,USE_WARNS),3)),$(call gnuc_min_ver,8.0)))
override WARN_OPTS := $(WARN_OPTS) -Wcast-align=strict -Wpointer-arith \
-Wfloat-conversion -Wdouble-promotion -Wlarger-than=1048576 -Wframe-larger-than=32768
endif

# Enable C11/C++11 standard, disallow K&R style functions on Clang 4.0+
ifneq (,$(call clang_min_ver,4.0))
override CC_STD  := -std=c11 -Wstrict-prototypes -Wold-style-definition
override CXX_STD := -std=c++11
endif

# Enable C11/C++11 standard, disallow K&R style functions on GCC 5.0+
ifneq (,$(call gcc_min_ver,5.0))
override CC_STD  := -std=c11 -Wstrict-prototypes -Wold-style-declaration -Wold-style-definition
override CXX_STD := -std=c++11
endif

# Set compiler-specific optimization options
# Enable -O2 unless USE_DEBUG_FULL is set, which enables -O0
# Enable -flto=auto on GCC 5.0+, -flto on Clang 5.0+
#
# i386:  Target i586 (Pentium I) by default
# ARM64: Disable outline atomics to prevent intrinsic call site intrusion
#
# Non-ELF targets (Windows, AmigaOS):
# Enable -flto-incremental on GCC 15.0+
# Enable -fno-plt -fno-semantic-interposition on GCC 6.0+
# Enable -fvisibility=hidden -Bsymbolic on GCC/Clang 4.0+
#
# Enable -fanalyzer on USE_ANALYZER on GCC 10.1+
override OPTIMIZE_OPTS := $(strip $(OPTIMIZE_OPTS) \
$(if $(filter i386,$(ARCH)), \
  $(if $(call gnuc_min_ver,3.0),$(if $(filter -march% -msse% -mfpmath%,$(CFLAGS)),,-march=i586))) \
$(if $(filter arm64,$(ARCH)), \
  $(if $(call gnuc_min_ver,9.0)$(call clang_min_ver,13.0),-mno-outline-atomics)) \
$(if $(LTO_SUPPORTED), \
  $(if $(call gcc_min_ver,5.0),-flto=auto) \
  $(if $(call clang_min_ver,5.0),-flto)) \
$(if $(filter-out windows amigaos,$(OS)), \
  $(if $(call gcc_min_ver,15.0),-flto-incremental=$(OBJDIR)) \
  $(if $(call gcc_min_ver,6.0),-fno-plt -fno-semantic-interposition) \
  $(if $(call gnuc_min_ver,4.0),-fvisibility=hidden -Bsymbolic)) \
$(if $(call var_use,USE_ANALYZER), \
  $(if $(call gcc_min_ver,10.1),-fanalyzer)))

# Set compiler-specific mandatory optimization options appended after user CFLAGS
# Override -Ofast into -O3 if detected
# Enable -fno-fast-math -fno-math-errno -frounding-math on GCC/Clang 4.0+
override MANDATORY_OPTS := $(strip \
$(if $(filter -Ofast,$(CFLAGS)),-O3) \
$(if $(call gnuc_min_ver,4.0),-fno-fast-math -fno-math-errno -frounding-math))

# Set compiler-specific warning & suppression options
#
# Enable -Wno-missing-braces on GCC 3.0+ & Clang 3.0+
# Enable -Wno-missing-field-initializers -Wfatal-errors on GCC 4.0+
#
# Enable -Wno-unknown-warning-option -Wno-unsupported-floating-point-opt -Wno-ignored-optimization-argument on Clang 3.0+
# Enable -Wno-missing-braces -Wno-missing-field-initializers -Wno-ignored-pragmas -Wno-atomic-alignment on Clang 3.0+
# Enable -Wdocumentation on Clang 4.0+
override WARN_OPTS := $(strip $(WARN_OPTS) \
$(if $(call gnuc_min_ver,3.0),-Wno-missing-braces) \
$(if $(call gcc_min_ver,4.0),-Wno-missing-field-initializers -Wfatal-errors) \
$(if $(call clang_min_ver,3.0),-Wno-unknown-warning-option -Wno-unsupported-floating-point-opt -Wno-ignored-optimization-argument) \
$(if $(call clang_min_ver,3.0),-Wno-missing-field-initializers -Wno-ignored-pragmas -Wno-atomic-alignment) \
$(if $(call clang_min_ver,4.0),-Wdocumentation))

# Produce final CFLAGS/LDFLAGS, strip excess spaces
override CPPFLAGS    := $(strip $(patsubst %,-I%,$(INCDIR) $(SRCDIR)) -D$(NAME_UPPER)_VERSION="$(VERSION)" $(CPPFLAGS))
override CFLAGS      := $(strip $(OPTIMIZE_OPTS) $(WARN_OPTS) $(CFLAGS) $(MANDATORY_OPTS))
override LDFLAGS     := $(strip $(LDFLAGS))
override PRE_LDFLAGS := $(if $(call gnuc_min_ver,4.0),$(call check_cc_flags,-Wl$(COMMA)--as-needed))

###################################################################################################
#
# Check previous build flags, trigger a rebuild if necessary
#
###################################################################################################

override CC_INFO      := $(OBJDIR)/cc_info.txt
override LD_INFO      := $(OBJDIR)/ld_info.txt
override CC_TRIGGER   := $(OBJDIR)/cc_trigger.txt
override LD_TRIGGER   := $(OBJDIR)/ld_trigger.txt
override CURR_CC_INFO := $(CC) $(CC_VERSION) $(CPPFLAGS) $(CFLAGS)
override CURR_LD_INFO := $(LDFLAGS)

# Separate CC_INFO and CC_TRIGGER to prevent repeated parsing of the Makefile when CC_TRIGGER dependency is overwritten
sinclude $(CC_INFO) $(LD_INFO)

ifneq ($(CURR_CC_INFO),$(call var_src,PREV_CC_INFO))
$(call install_string,PREV_CC_INFO := $(CURR_CC_INFO),$(CC_INFO))
$(call install_string,This file triggers source rebuild,$(CC_TRIGGER))
endif

ifneq ($(CURR_LD_INFO),$(call var_src,PREV_LD_INFO))
$(call install_string,PREV_LD_INFO := $(CURR_LD_INFO),$(LD_INFO))
$(call install_string,This file triggers linker rebuild,$(LD_TRIGGER))
endif

# Provide compile_flags for clangd
$(call install_string,$(subst $(SPACE),$(NEWLINE),$(CPPFLAGS) $(WARN_OPTS)),compile_flags.txt)

###################################################################################################
#
# Print build information
#
###################################################################################################

# Show the project logo if terminal supports unicode
ifneq (,$(if $(findstring UTF,$(call var_def,LANG)),$(LOGO)))
$(call println,$(RESET))
$(call println,$(LOGO))
endif

ifneq (,$(call var_use,VERBOSE))
override VERBOSE_VARS := HOST_UNAME HOST_OS HOST_ARCH CC_BRAND CC_VERSION CC_TRIPLET LTO_SUPPORTED \
OS ARCH GIT_DESCRIBE USEFLAGS_ON CC_STD CXX_STD CPPFLAGS CFLAGS LDFLAGS SRC_OFF LIBS_PKG BIN_TARGETS LIB_TARGETS
$(call println,$(RESET))
$(call log_info,Verbose build info:)
override _ := $(foreach var,$(VERBOSE_VARS),$(call println,$(GREEN)$(var)$(RESET): $($(var))))
endif

# Print build information
$(call println,$(RESET))
$(call println,$(TEXT)Detected OS: $(GREEN)$(OS_PRETTY)$(RESET))
$(call println,$(TEXT)Detected CC: $(GREEN)$(CC_PRETTY)$(RESET))
$(call println,$(TEXT)Target arch: $(GREEN)$(ARCH)$(RESET))
$(call println,$(TEXT)Version:     $(GREEN)$(NAME) $(VERSION)$(RESET))
$(call println,$(RESET))

# Allow tests to run with USE_LIB_SHARING
export LD_LIBRARY_PATH   := $(BUILDDIR)$(if $(call var_def,LD_LIBRARY_PATH),:$(LD_LIBRARY_PATH))
export DYLD_LIBRARY_PATH := $(BUILDDIR)$(if $(call var_def,DYLD_LIBRARY_PATH),:$(DYLD_LIBRARY_PATH))

###################################################################################################
#
# Make targets
#
###################################################################################################

# Handle header dependencies generated by the compiler
sinclude $(DEPS)

# Ignore deleted header/dependency files
%.h %.hpp %.hh %.hxx %.d %.c %.cpp %.cxx %.cc %.m:
	@:

# Ignore not yet created CC/LD rebuild triggers
$(call path_shell,$(CC_TRIGGER) $(LD_TRIGGER)):
	@:



# C object files (.c)
$(call path_shell,$(OBJDIR)/%.o: %.c Makefile project.mk $(CC_TRIGGER))
	$(call println,$(TEXT)[$(YELLOW)CC$(TEXT)] $< $(RESET))
	@$(foreach out,$(call path_wrap,$@),$(call shell_esc,$(CC) $(CC_STD) $(CPPFLAGS) $(CFLAGS) $(if $(CC_IS_GNU),-MMD -MF $(patsubst %.o,%.d,$(out))) -o $(out) -c $(call path_wrap,$<)))
	@$(if $(call var_use,USE_ANALYZER),$(if $(call clang_min_ver,9.0),$(call shell_esc,$(CC) $(CPPFLAGS) $(CFLAGS) --analyze $(call path_wrap,$<))))



# C++ object files (.cpp / .cxx / .cc)
$(call path_shell,$(OBJDIR)/%.o: %.cpp Makefile project.mk $(CC_TRIGGER))
	$(call println,$(TEXT)[$(YELLOW)CC$(TEXT)] $< $(RESET))
	@$(foreach out,$(call path_wrap,$@),$(call shell_esc,$(CXX) $(CXX_STD) $(CPPFLAGS) $(CFLAGS) $(if $(CC_IS_GNU),-MMD -MF $(patsubst %.o,%.d,$(out))) -o $(out) -c $(call path_wrap,$<)))
	@$(if $(call var_use,USE_ANALYZER),$(if $(call clang_min_ver,9.0),$(call shell_esc,$(CC) $(CPPFLAGS) $(CFLAGS) --analyze $(call path_wrap,$<))))

$(call path_shell,$(OBJDIR)/%.o: %.cxx Makefile project.mk $(CC_TRIGGER))
	$(call println,$(TEXT)[$(YELLOW)CC$(TEXT)] $< $(RESET))
	@$(foreach out,$(call path_wrap,$@),$(call shell_esc,$(CXX) $(CXX_STD) $(CPPFLAGS) $(CFLAGS) $(if $(CC_IS_GNU),-MMD -MF $(patsubst %.o,%.d,$(out))) -o $(out) -c $(call path_wrap,$<)))
	@$(if $(call var_use,USE_ANALYZER),$(if $(call clang_min_ver,9.0),$(call shell_esc,$(CC) $(CPPFLAGS) $(CFLAGS) --analyze $(call path_wrap,$<))))

$(call path_shell,$(OBJDIR)/%.o: %.cc Makefile project.mk $(CC_TRIGGER))
	$(call println,$(TEXT)[$(YELLOW)CC$(TEXT)] $< $(RESET))
	@$(foreach out,$(call path_wrap,$@),$(call shell_esc,$(CXX) $(CXX_STD) $(CPPFLAGS) $(CFLAGS) $(if $(CC_IS_GNU),-MMD -MF $(patsubst %.o,%.d,$(out))) -o $(out) -c $(call path_wrap,$<)))
	@$(if $(call var_use,USE_ANALYZER),$(if $(call clang_min_ver,9.0),$(call shell_esc,$(CC) $(CPPFLAGS) $(CFLAGS) --analyze $(call path_wrap,$<))))



# Objective-C object files (.m), compiled by the C compiler (macOS Cocoa backend)
$(call path_shell,$(OBJDIR)/%.o: %.m Makefile project.mk $(CC_TRIGGER))
	$(call println,$(TEXT)[$(YELLOW)CC$(TEXT)] $< $(RESET))
	@$(foreach out,$(call path_wrap,$@),$(call shell_esc,$(CC) $(CC_STD) $(CPPFLAGS) $(CFLAGS) $(if $(CC_IS_GNU),-MMD -MF $(patsubst %.o,%.d,$(out))) -o $(out) -c $(call path_wrap,$<)))



# Binaries
$(call path_shell,$(BIN_TARGETS): $(BIN_OBJ) $(BIN_LIBS) $(CC_TRIGGER) $(LD_TRIGGER))
	$(call println,$(TEXT)[$(GREEN)LD$(TEXT)] $@ $(RESET))
	@$(foreach out,$(call path_wrap,$@),$(call shell_esc,$(call bin_target_ld,$(out)) $(CPPFLAGS) $(CFLAGS) $(PRE_LDFLAGS) $(call bin_target_link,$(out)) $(LDFLAGS) -o $(out)))



# Set -Wl,-soname,<libname.so> for an ELF target, set -Wl,--out-implib,$@.a for Windows libraries
override shared_extra = $(if $(filter .so,$(LIB_EXT)),-Wl$(COMMA)-soname$(COMMA)$(notdir $1).0) $(if $(filter windows,$(OS)),-Wl$(COMMA)--out-implib$(COMMA)$(patsubst %$(LIB_EXT),%.a,$1))

# Shared libraries
$(call path_shell,$(LIB_TARGETS): $(LIB_OBJ) $(CC_TRIGGER) $(LD_TRIGGER))
	$(call println,$(TEXT)[$(GREEN)LD$(TEXT)] $@ $(RESET))
	@$(foreach out,$(call path_wrap,$@),$(call shell_esc,$(call lib_target_ld,$(out)) $(CPPFLAGS) $(CFLAGS) $(PRE_LDFLAGS) $(call lib_target_link,$(out)) $(LDFLAGS) -shared $(call shared_extra,$(out)) -o $(out)))

# Generate internal shared library dependencies, up to 8 levels of nesting is supported
override libs_dependants = $(filter-out $1,$(strip $(foreach lib,$(LIB_TARGETS),$(if $(filter-out $1,$(call lib_target_own_libs,$(lib))),,$(lib)))))
override LIBS_PREV := $(call libs_dependants,)
override LIBS_NEXT := $(filter-out $(LIBS_PREV),$(call libs_dependants,$(LIBS_PREV)))
ifneq (,$(LIBS_NEXT))
$(call gen_dependency,$(LIBS_NEXT),$(LIBS_PREV))
override LIBS_PREV := $(LIBS_PREV) $(LIBS_NEXT)
override LIBS_NEXT := $(filter-out $(LIBS_PREV),$(call libs_dependants,$(LIBS_PREV)))
ifneq (,$(LIBS_NEXT))
$(call gen_dependency,$(LIBS_NEXT),$(LIBS_PREV))
override LIBS_PREV := $(LIBS_PREV) $(LIBS_NEXT)
override LIBS_NEXT := $(filter-out $(LIBS_PREV),$(call libs_dependants,$(LIBS_PREV)))
ifneq (,$(LIBS_NEXT))
$(call gen_dependency,$(LIBS_NEXT),$(LIBS_PREV))
override LIBS_PREV := $(LIBS_PREV) $(LIBS_NEXT)
override LIBS_NEXT := $(filter-out $(LIBS_PREV),$(call libs_dependants,$(LIBS_PREV)))
ifneq (,$(LIBS_NEXT))
$(call gen_dependency,$(LIBS_NEXT),$(LIBS_PREV))
override LIBS_PREV := $(LIBS_PREV) $(LIBS_NEXT)
override LIBS_NEXT := $(filter-out $(LIBS_PREV),$(call libs_dependants,$(LIBS_PREV)))
ifneq (,$(LIBS_NEXT))
$(call gen_dependency,$(LIBS_NEXT),$(LIBS_PREV))
override LIBS_PREV := $(LIBS_PREV) $(LIBS_NEXT)
override LIBS_NEXT := $(filter-out $(LIBS_PREV),$(call libs_dependants,$(LIBS_PREV)))
ifneq (,$(LIBS_NEXT))
$(call gen_dependency,$(LIBS_NEXT),$(LIBS_PREV))
override LIBS_PREV := $(LIBS_PREV) $(LIBS_NEXT)
override LIBS_NEXT := $(filter-out $(LIBS_PREV),$(call libs_dependants,$(LIBS_PREV)))
ifneq (,$(LIBS_NEXT))
$(call gen_dependency,$(LIBS_NEXT),$(LIBS_PREV))
override LIBS_PREV := $(LIBS_PREV) $(LIBS_NEXT)
override LIBS_NEXT := $(filter-out $(LIBS_PREV),$(call libs_dependants,$(LIBS_PREV)))
ifneq (,$(LIBS_NEXT))
$(call gen_dependency,$(LIBS_NEXT),$(LIBS_PREV))
endif
endif
endif
endif
endif
endif
endif
endif



# Static libraries
$(call path_shell,$(LIB_STATIC_TARGETS): $(LIB_OBJ))
	$(call println,$(TEXT)[$(GREEN)AR$(TEXT)] $@ $(RESET))
	@$(call shell_esc,$(AR) -rcs $(call path_wrap,$@) $(call lib_static_obj,$(call path_wrap,$@)))



# Phony targets
.PHONY: all         # Build everything (Default)
all: lib bin

.PHONY: bin         # Build executables
bin: $(call path_shell,$(BIN_TARGETS))

.PHONY: lib         # Build shared / static libraries
lib: $(if $(call var_use,USE_LIB),$(LIB_TARGETS)) $(if $(call var_use,USE_LIB_STATIC),$(LIB_STATIC_TARGETS))



.PHONY: test        # Run tests
test: bin



override CPPCHECK_OPTS := -q -f -j$(JOBS) --std=c99 -D__CPPCHECK__ -I$(SRCDIR) --inline-suppr --error-exitcode=1 --cppcheck-build-dir=$(OBJDIR) \
--check-level=exhaustive --enable=all --inconclusive \
--suppress=unmatchedSuppression --suppress=missingIncludeSystem --suppress=missingInclude --suppress=syntaxError --suppress=unreadVariable \
--suppress=cstyleCast --suppress=unusedFunction --suppress=constParameterCallback --suppress=useStandardLibrary --suppress=uselessAssignmentArg \
--suppress=va_list_usedBeforeStarted --suppress=noExplicitConstructor  \
$(if $(filter l,$(call num_cmp,$(USE_WARNS),7)),--suppress=constParameterPointer --suppress=constVariablePointer --suppress=variableScope) \
$(if $(filter l,$(call num_cmp,$(USE_WARNS),6)),--suppress=knownConditionTrueFalse --suppress=badBitmaskCheck) \
$(if $(filter l,$(call num_cmp,$(USE_WARNS),5)),--suppress=unusedStructMember) \
$(if $(filter l,$(call num_cmp,$(USE_WARNS),4)),--suppress=constVariable) \
$(if $(filter l,$(call num_cmp,$(USE_WARNS),3)),--suppress=funcArgNamesDifferent --suppress=shadowVariable) \
$(if $(filter l,$(call num_cmp,$(USE_WARNS),2)),--suppress=truncLongCastAssignment --suppress=unreachableCode) \
$(call var_src,CPPCHECK_SUPPRESSIONS)

.PHONY: cppcheck    # Run cppcheck static analysis
cppcheck:
	$(call log_info,Running cppcheck static analysis)
	@cppcheck $(CPPCHECK_OPTS) $(SRCDIR)



.PHONY: clean       # Clean the build directory
clean:
	$(call log_info,Cleaning up)
ifneq (,$(HOST_POSIX))
	@-rm $(BIN_TARGETS) $(LIB_TARGETS) $(LIB_STATIC_TARGETS) $(NULL_STDERR) ||:
	@-rm -r $(OBJDIR) $(NULL_STDERR) ||:
else
	@-del $(subst /,\, $(BIN_TARGETS) $(LIB_TARGETS) $(LIB_STATIC_TARGETS)) $(NULL_STDERR) ||:
	@-rmdir /S /Q $(subst /,\, $(OBJDIR)) $(NULL_STDERR) ||:
endif
	@-rmdir $(BUILDDIR) $(NULL_STDERR) ||:



# System-wide install
DESTDIR ?= $(if $(filter windows,$(HOST_OS)),pkg)
PREFIX  ?= /usr

# Handle all the weird GNU-style installation variables
prefix      ?= $(PREFIX)
exec_prefix ?= $(prefix)
bindir      ?= $(exec_prefix)/bin
libdir      ?= $(exec_prefix)/lib
includedir  ?= $(prefix)/include
datarootdir ?= $(prefix)/share
datadir     ?= $(datarootdir)

# Generate pkg-config spec contents for a library $1 with suffix $2 (Like _static)
override define gen_pkg_config
prefix=$(prefix)
exec_prefix=$(exec_prefix)
libdir=$(libdir)
includedir=$(includedir)

Name: $1
Description: $(DESC)
URL: $(URL)
Version: $(VERSION)
Requires.private: $(LIBS_PKG)
Libs: -L$(libdir) -l$1$2
Cflags: -I$(includedir) -I$(includedir)/$1 $(if $2,,-DLIB$(call toupper,$1)_SHARED)
endef

.PHONY: install     # Install the package
install: all
	$(call log_info,Installing to $(DESTDIR)$(prefix))
# Install binaries
	$(foreach bin,$(BIN_TARGETS),$(call install_file,$(bin),$(DESTDIR)$(bindir)/$(call bin_base,$(bin))$(BIN_EXT),0755))
# Install headers
	$(if $(INCDIR),$(if $(call var_use,USE_LIB)$(call var_use,USE_LIB_STATIC),$(foreach header,$(call recursive_match,$(INCDIR),*.h *.hpp *.hh *.hxx),$(call install_file,$(header),$(DESTDIR)$(includedir)/$(patsubst $(INCDIR)/%,%,$(header)),0644))))
ifeq ($(LIB_EXT),.so)
# Install shared libraries with soname suffix
	$(foreach lib,$(if $(call var_use,USE_LIB),$(LIB_TARGETS)),\
		$(call install_file,$(lib),$(DESTDIR)$(libdir)/lib$(call lib_base,$(lib))$(LIB_EXT).0.0,0755) \
		$(call shell_ex,ln -sf lib$(call lib_base,$(lib))$(LIB_EXT).0.0 $(DESTDIR)$(libdir)/lib$(call lib_base,$(lib))$(LIB_EXT).0) \
		$(call shell_ex,ln -sf lib$(call lib_base,$(lib))$(LIB_EXT).0 $(DESTDIR)$(libdir)/lib$(call lib_base,$(lib))$(LIB_EXT)) \
	)
else
	$(foreach lib,$(if $(call var_use,USE_LIB),$(LIB_TARGETS)),$(call install_file,$(lib),$(DESTDIR)$(libdir)/lib$(call lib_base,$(lib))$(LIB_EXT),0755))
endif
# Install pkg-config pc files for shared libraries
	$(foreach lib,$(if $(call var_use,USE_LIB),$(LIB_TARGETS)),$(call install_string,$(call gen_pkg_config,$(call lib_base,$(lib)),),$(DESTDIR)$(libdir)/pkgconfig/$(call lib_base,$(lib)).pc))
# Install static libraries
	$(foreach lib,$(if $(call var_use,USE_LIB_STATIC),$(LIB_STATIC_TARGETS)),$(call install_file,$(lib),$(DESTDIR)$(libdir)/lib$(call lib_static_base,$(lib))_static.a,0644))
# Install pkg-config pc files for static libraries
	$(foreach lib,$(if $(call var_use,USE_LIB_STATIC),$(LIB_STATIC_TARGETS)),$(call install_string,$(call gen_pkg_config,$(call lib_static_base,$(lib)),_static),$(DESTDIR)$(libdir)/pkgconfig/$(call lib_static_base,$(lib))-static.pc))
# Install licenses
	$(foreach license,$(call safe_wildcard,LICENSE*),$(call install_file,$(license),$(DESTDIR)$(datadir)/licenses/$(NAME_LOWER)/$(license),0644))
	@:



.PHONY: help        # Show this help message
help:
	$(call log_info,Available make useflags:$(foreach useflag, $(USEFLAGS),$(NEWLINE) $(useflag)=$($(useflag))))
	$(call println,$(RESET))
	$(call log_info,Available make targets:$(subst #,$(TEXT),$(subst .PHONY:,$(NEWLINE)$(GREEN),$(shell grep '^.PHONY:' Makefile $(NULL_STDERR)))))
	$(call println,$(RESET))
	@:

.PHONY: info        # Show this help message
info: help

.PHONY: list        # Show this help message
list: help
