# gesso engine, gesso2c, and the QA gate. The Filo runtime comes from the
# clang_filo checkout beside this repository.
CC = xcrun clang
LLVM ?= /opt/homebrew/opt/llvm/bin
FILO ?= ../../clang_filo
LAVAGNA ?= ../../lavagna
MACOS_MIN = 14.0
FRAMEWORKS = -framework Cocoa -framework Metal -framework QuartzCore -framework UniformTypeIdentifiers
WARN = -Wall -Wextra -Werror -Wshadow -Wconversion -Wdouble-promotion -Wundef
CFLAGS ?= -std=c11 -O2 $(WARN) -I$(FILO)
SAN = -std=c11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all $(WARN) -I$(FILO)
ENGINE = gesso.c gesso_filo.c
FILO_SRC = $(FILO)/filo.c $(FILO)/filo_nolibc.c
HDRS = gesso.h gesso_filo.h
APP_SRC = app.c editor.c ui.c png.c gesso_png.c $(ENGINE)
APP = build/gessetto.app
CASES = testdata/corpus/*/
TIDY_CHECKS = bugprone-*,cert-*,clang-analyzer-*,readability-*,-readability-magic-numbers,-readability-function-cognitive-complexity,-readability-identifier-length,-readability-braces-around-statements,-bugprone-easily-swappable-parameters,-cert-err33-c,-readability-else-after-return,-readability-math-missing-parentheses,-readability-uppercase-literal-suffix,-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling

.PHONY: all app ios run ui editor png FORCE corpus corpus-update fmt fmt-check tidy check freestanding qa clean

all: build/gesso2c build/gesso-apply build/corpus app

build/gesso-apply: gesso_apply.c $(ENGINE) $(HDRS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ gesso_apply.c $(ENGINE) $(FILO_SRC)

# Every PNG case against what Go's decoder makes of it, under the sanitizers.
png: png_test.c png.c gesso_png.c png.h gesso_png.h $(ENGINE) $(HDRS)
	@mkdir -p build
	$(CC) $(SAN) -o build/png_test png_test.c png.c gesso_png.c $(ENGINE) $(FILO_SRC)
	./build/png_test testdata/png/*.png

app: $(APP)

run: app
	open $(APP)

build/font.h: assets/font.gesso build/gesso2c
	./build/gesso2c -n font assets/font.gesso > $@

build/tools.h: assets/tools.gesso build/gesso2c
	./build/gesso2c -n tools assets/tools.gesso > $@

IOS_MIN = 17.0
SIM_SDK = $(shell xcrun --sdk iphonesimulator --show-sdk-path)
SIM = -target arm64-apple-ios$(IOS_MIN)-simulator -isysroot $(SIM_SDK)
IOS_FRAMEWORKS = -framework UIKit -framework Foundation -framework CoreGraphics -framework Metal \
	-framework QuartzCore -framework UniformTypeIdentifiers -lobjc
LAVAGNA_SIM = $(LAVAGNA)/build/ios-sim/liblavagna.a
IOS_APP = build/ios-sim/gessetto.app

# The backend comes built from the lavagna checkout beside this one.
LAVAGNA_LIB = $(LAVAGNA)/build/macos/liblavagna.a

$(LAVAGNA_LIB): FORCE
	$(MAKE) -C $(LAVAGNA) lib

$(LAVAGNA_SIM): FORCE
	$(MAKE) -C $(LAVAGNA) lib-ios-sim

ios: $(IOS_APP)

# The same program on the iPad simulator: signed ad hoc, documents in Files.
$(IOS_APP): $(APP_SRC) $(HDRS) editor.h ui.h build/font.h build/tools.h $(LAVAGNA_SIM)
	@mkdir -p $@
	$(CC) -std=c11 -O2 $(WARN) $(SIM) -I$(FILO) -Ibuild -I$(LAVAGNA) -Wl,-dead_strip \
		$(IOS_FRAMEWORKS) -o $@/gessetto $(APP_SRC) $(FILO_SRC) $(LAVAGNA_SIM)
	@printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>' \
		'<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
		'<plist version="1.0"><dict>' \
		'<key>CFBundleExecutable</key><string>gessetto</string>' \
		'<key>CFBundleIdentifier</key><string>br.eti.crg.gessetto</string>' \
		'<key>CFBundleName</key><string>gessetto</string>' \
		'<key>CFBundlePackageType</key><string>APPL</string>' \
		'<key>CFBundleVersion</key><string>1</string>' \
		'<key>CFBundleShortVersionString</key><string>0.1</string>' \
		'<key>CFBundleSupportedPlatforms</key><array><string>iPhoneSimulator</string></array>' \
		'<key>MinimumOSVersion</key><string>$(IOS_MIN)</string>' \
		'<key>UIDeviceFamily</key><array><integer>2</integer></array>' \
		'<key>UILaunchScreen</key><dict/>' \
		'<key>UIFileSharingEnabled</key><true/>' \
		'<key>LSSupportsOpeningDocumentsInPlace</key><true/>' \
		'<key>CFBundleDocumentTypes</key><array>' \
		'<dict><key>CFBundleTypeName</key><string>gesso image</string><key>CFBundleTypeRole</key><string>Editor</string>' \
		'<key>LSHandlerRank</key><string>Owner</string><key>LSItemContentTypes</key><array><string>br.eti.crg.gesso</string></array></dict>' \
		'<dict><key>CFBundleTypeName</key><string>PNG image</string><key>CFBundleTypeRole</key><string>Viewer</string>' \
		'<key>LSHandlerRank</key><string>Alternate</string><key>LSItemContentTypes</key><array><string>public.png</string></array></dict>' \
		'</array>' \
		'<key>UTExportedTypeDeclarations</key><array><dict>' \
		'<key>UTTypeIdentifier</key><string>br.eti.crg.gesso</string>' \
		'<key>UTTypeDescription</key><string>gesso image</string>' \
		'<key>UTTypeConformsTo</key><array><string>public.plain-text</string></array>' \
		'<key>UTTypeTagSpecification</key><dict><key>public.filename-extension</key><array><string>gesso</string></array></dict>' \
		'</dict></array>' \
		'<key>UISupportedInterfaceOrientations~ipad</key><array><string>UIInterfaceOrientationPortrait</string><string>UIInterfaceOrientationLandscapeLeft</string><string>UIInterfaceOrientationLandscapeRight</string><string>UIInterfaceOrientationPortraitUpsideDown</string></array>' \
		'<key>UIApplicationSceneManifest</key><dict>' \
		'<key>UIApplicationSupportsMultipleScenes</key><false/>' \
		'<key>UISceneConfigurations</key><dict><key>UIWindowSceneSessionRoleApplication</key><array><dict>' \
		'<key>UISceneConfigurationName</key><string>Default</string>' \
		'<key>UISceneDelegateClassName</key><string>LavSceneDelegate</string>' \
		'</dict></array></dict></dict>' \
		'</dict></plist>' > $@/Info.plist
	codesign --force --sign - $@ 2>/dev/null

FORCE:

$(APP): $(APP_SRC) $(HDRS) editor.h ui.h build/font.h build/tools.h $(LAVAGNA_LIB)
	@mkdir -p $@/Contents/MacOS $@/Contents/Resources
	$(CC) $(CFLAGS) -Ibuild -I$(LAVAGNA) -mmacosx-version-min=$(MACOS_MIN) -Wl,-dead_strip -Wl,-x \
		$(FRAMEWORKS) -o $@/Contents/MacOS/gessetto $(APP_SRC) $(FILO_SRC) $(LAVAGNA_LIB)
	cp ../assets/gessetto.icns $@/Contents/Resources/
	@printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>' \
		'<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
		'<plist version="1.0"><dict>' \
		'<key>CFBundleExecutable</key><string>gessetto</string>' \
		'<key>CFBundleIconFile</key><string>gessetto</string>' \
		'<key>CFBundleIdentifier</key><string>br.eti.crg.gessetto</string>' \
		'<key>CFBundleName</key><string>gessetto</string>' \
		'<key>CFBundlePackageType</key><string>APPL</string>' \
		'<key>LSMinimumSystemVersion</key><string>$(MACOS_MIN)</string>' \
		'<key>NSHighResolutionCapable</key><true/>' \
		'<key>CFBundleDocumentTypes</key><array>' \
		'<dict><key>CFBundleTypeName</key><string>gesso image</string><key>CFBundleTypeRole</key><string>Editor</string>' \
		'<key>LSHandlerRank</key><string>Owner</string><key>LSItemContentTypes</key><array><string>br.eti.crg.gesso</string></array></dict>' \
		'<dict><key>CFBundleTypeName</key><string>PNG image</string><key>CFBundleTypeRole</key><string>Viewer</string>' \
		'<key>LSHandlerRank</key><string>Alternate</string><key>LSItemContentTypes</key><array><string>public.png</string></array></dict>' \
		'</array>' \
		'<key>UTExportedTypeDeclarations</key><array><dict>' \
		'<key>UTTypeIdentifier</key><string>br.eti.crg.gesso</string>' \
		'<key>UTTypeDescription</key><string>gesso image</string>' \
		'<key>UTTypeConformsTo</key><array><string>public.plain-text</string></array>' \
		'<key>UTTypeTagSpecification</key><dict><key>public.filename-extension</key><array><string>gesso</string></array></dict>' \
		'</dict></array>' \
		'</dict></plist>' > $@/Contents/Info.plist

# A recorded session through the real window (it opens one; a 2x screen):
# what it saves must be the image checked by hand.
ui: app
	rm -f build/ui-basic.gesso
	LAVAGNA_EVENTS=testdata/ui/basic.events ./$(APP)/Contents/MacOS/gessetto
	cmp build/ui-basic.gesso testdata/ui/basic.gesso
	rm -f build/ui-frames.gesso
	LAVAGNA_EVENTS=testdata/ui/frames.events ./$(APP)/Contents/MacOS/gessetto
	cmp build/ui-frames.gesso testdata/ui/frames.gesso

# The tools without a window, under the sanitizers.
editor: editor_test.c editor.c gesso.c editor.h gesso.h
	@mkdir -p build
	$(CC) $(SAN) -o build/editor_test editor_test.c editor.c gesso.c
	./build/editor_test

build/gesso2c: gesso2c.c $(ENGINE) $(HDRS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ gesso2c.c $(ENGINE) $(FILO_SRC)

build/corpus: corpus.c $(ENGINE) $(HDRS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ corpus.c $(ENGINE) $(FILO_SRC)

build/corpus_san: corpus.c $(ENGINE) $(HDRS)
	@mkdir -p build
	$(CC) $(SAN) -o $@ corpus.c $(ENGINE) $(FILO_SRC)

# Every case under the sanitizers.
corpus: build/corpus_san
	./build/corpus_san $(CASES)

corpus-update: build/corpus
	./build/corpus -update $(CASES)

fmt:
	$(LLVM)/clang-format -i *.c *.h

fmt-check:
	$(LLVM)/clang-format --dry-run --Werror *.c *.h

tidy:
	$(LLVM)/clang-tidy --quiet --warnings-as-errors='*' --checks='$(TIDY_CHECKS)' \
		$(ENGINE) corpus.c gesso2c.c gesso_apply.c editor.c editor_test.c ui.c app.c png.c \
		gesso_png.c png_test.c -- -std=c11 -I$(FILO) -Ibuild -I$(LAVAGNA)

check:
	cppcheck --enable=warning,style,performance,portability --inline-suppr \
		--suppress=missingIncludeSystem --error-exitcode=1 -I$(FILO) -I$(LAVAGNA) \
		$(ENGINE) corpus.c gesso2c.c gesso_apply.c editor.c editor_test.c app.c png.c gesso_png.c \
		png_test.c

# The engine needs nothing from libc but the four memory functions.
freestanding:
	@mkdir -p build
	$(LLVM)/clang --target=wasm32 -ffreestanding -nostdlib -fno-builtin -c -std=c11 -O2 $(WARN) \
		-isystem freestanding/include -o build/gesso_wasm.o gesso.c
	$(LLVM)/clang --target=wasm32 -ffreestanding -nostdlib -fno-builtin -c -std=c11 -O2 $(WARN) \
		-isystem freestanding/include -o build/editor_wasm.o editor.c
	$(LLVM)/clang --target=wasm32 -ffreestanding -nostdlib -fno-builtin -c -std=c11 -O2 $(WARN) \
		-isystem freestanding/include -o build/png_wasm.o png.c

qa: all fmt-check corpus editor png tidy check freestanding

clean:
	rm -rf build
