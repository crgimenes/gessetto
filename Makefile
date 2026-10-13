# gessetto: the C core (gesso engine, editor, PNG, the facade the app sees),
# its command-line tools, and the Swift app on top. The Filo runtime comes
# from the clang_filo checkout beside this repository.
CC = xcrun clang
LLVM ?= /opt/homebrew/opt/llvm/bin
FILO ?= ../clang_filo

WARN = -Wall -Wextra -Werror -Wshadow -Wconversion -Wdouble-promotion -Wundef
CFLAGS ?= -std=c11 -O2 $(WARN) -I$(FILO)
SAN = -std=c11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all $(WARN) -I$(FILO)
ENGINE = gesso.c gesso_filo.c
FILO_SRC = $(FILO)/filo.c $(FILO)/filo_nolibc.c
HDRS = gesso.h gesso_filo.h
CORE = $(ENGINE) editor.c png.c gesso_png.c gesso_c.c gessetto.c
OWN = $(CORE) corpus.c gesso2c.c gesso_apply.c editor_test.c png_test.c gessetto_test.c
CASES = testdata/corpus/*/
TIDY_CHECKS = bugprone-*,cert-*,clang-analyzer-*,readability-*,-readability-magic-numbers,-readability-function-cognitive-complexity,-readability-identifier-length,-readability-braces-around-statements,-bugprone-easily-swappable-parameters,-cert-err33-c,-readability-else-after-return,-readability-math-missing-parentheses,-readability-uppercase-literal-suffix,-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling

APP = build/gessetto.app
SIM_APP = build/gessetto-sim.app

.PHONY: all app run mac-store ios-sim ios-store ios-upload xcframework swift-build swift-test swift-fmt editor png gessetto \
	formats corpus corpus-update fmt fmt-check tidy check freestanding qa clean

all: build/gesso2c build/gesso-apply build/corpus

build/gesso2c: gesso2c.c gesso_c.c gesso_c.h $(ENGINE) $(HDRS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ gesso2c.c gesso_c.c $(ENGINE) $(FILO_SRC)

build/gesso-apply: gesso_apply.c $(ENGINE) $(HDRS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ gesso_apply.c $(ENGINE) $(FILO_SRC)

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

# The tools without a window, under the sanitizers.
editor: editor_test.c editor.c gesso.c editor.h gesso.h
	@mkdir -p build
	$(CC) $(SAN) -o build/editor_test editor_test.c editor.c gesso.c
	./build/editor_test

# Every PNG case against what Go's decoder made of it, under the sanitizers.
png: png_test.c png.c gesso_png.c png.h gesso_png.h $(ENGINE) $(HDRS)
	@mkdir -p build
	$(CC) $(SAN) -o build/png_test png_test.c png.c gesso_png.c $(ENGINE) $(FILO_SRC)
	./build/png_test testdata/png/*.png

# The facade the app drives, the undo bridge above all.
gessetto: gessetto_test.c gessetto.h $(CORE) $(HDRS) editor.h
	@mkdir -p build
	$(CC) $(SAN) -o build/gessetto_test gessetto_test.c $(CORE) $(FILO_SRC)
	./build/gessetto_test

# The formats end to end: an image made by script, saved, read back and
# saved again byte for byte, then turned into a header a game compiles as
# strict C and checks.
formats: build/gesso-apply build/gesso2c testdata/formats/hero.filo testdata/formats/hero_test.c
	./build/gesso-apply testdata/formats/hero.filo > build/hero.gesso
	printf '(frame-select 0)' | ./build/gesso-apply -in build/hero.gesso - > build/hero-again.gesso
	cmp build/hero.gesso build/hero-again.gesso
	./build/gesso2c build/hero.gesso > build/hero.h
	$(CC) -std=c11 -pedantic $(WARN) -Ibuild -o build/hero_test testdata/formats/hero_test.c
	./build/hero_test

# The C core for the Swift package: macOS, iOS and simulator slices. It only
# packages; the warning gates are the targets above, tidy and check.
xcframework:
	tools/xcframework.sh "$(CC)" "-std=c11 -O2 -I$(FILO)" -- $(CORE) $(FILO_SRC)

swift-fmt:
	swift format lint --strict --recursive swift/Sources swift/Tests swift/Package.swift

swift-build: xcframework
	cd swift && swift build -Xswiftc -warnings-as-errors

swift-test: xcframework
	cd swift && swift test

MAC_ARCHS = --arch arm64 --arch x86_64

# Local bundle, universal, sandboxed as in the store, ad-hoc signed.
app: xcframework
	cd swift && swift build -c release $(MAC_ARCHS) -Xswiftc -warnings-as-errors
	rm -rf $(APP)
	mkdir -p $(APP)/Contents/MacOS $(APP)/Contents/Resources
	cp "$$(cd swift && swift build -c release $(MAC_ARCHS) --show-bin-path)/gessetto" $(APP)/Contents/MacOS/
	cp assets/Info.plist $(APP)/Contents/Info.plist
	xcrun actool assets/gessetto.icon --compile $(APP)/Contents/Resources --platform macosx \
		--minimum-deployment-target 14.0 --target-device mac --app-icon gessetto \
		--output-partial-info-plist build/icon-mac.plist --output-format human-readable-text >/dev/null
	/usr/libexec/PlistBuddy -c "Merge build/icon-mac.plist" $(APP)/Contents/Info.plist
	cp assets/PrivacyInfo.xcprivacy $(APP)/Contents/Resources/
	plutil -lint $(APP)/Contents/Info.plist
	codesign --force --sign - --entitlements assets/gessetto.entitlements $(APP)

run: app
	open $(APP)

# The Mac app to TestFlight and the Mac App Store; tools/mac-store.sh -h.
mac-store: app
	tools/mac-store.sh

# The same app for the iPad simulator; documents through the system's
# browser (Files, iCloud Drive).
ios-sim: xcframework
	cd swift && xcodebuild -scheme Gessetto -destination 'generic/platform=iOS Simulator' \
		ARCHS=arm64 PRODUCT_BUNDLE_IDENTIFIER=br.eti.crg.gessetto -derivedDataPath ../build/dd-ios -quiet build
	rm -rf $(SIM_APP)
	mkdir -p $(SIM_APP)
	cp build/dd-ios/Build/Products/Debug-iphonesimulator/gessetto $(SIM_APP)/gessetto
	cp assets/Info-iOS.plist $(SIM_APP)/Info.plist
	xcrun actool assets/gessetto.icon --compile $(SIM_APP) --platform iphonesimulator \
		--minimum-deployment-target 17.0 --target-device ipad --app-icon gessetto \
		--output-partial-info-plist build/icon-sim.plist --output-format human-readable-text >/dev/null
	/usr/libexec/PlistBuddy -c "Merge build/icon-sim.plist" $(SIM_APP)/Info.plist
	cp assets/PrivacyInfo.xcprivacy $(SIM_APP)/
	plutil -lint $(SIM_APP)/Info.plist
	codesign --force --sign - $(SIM_APP)

# The iPad app for TestFlight and the App Store; tools/ios-store.sh -h.
ios-store: xcframework
	tools/ios-store.sh

# What ios-store made, to TestFlight; tools/ios-upload.sh -h.
ios-upload:
	tools/ios-upload.sh

fmt:
	$(LLVM)/clang-format -i *.c *.h

fmt-check:
	$(LLVM)/clang-format --dry-run --Werror *.c *.h

tidy:
	$(LLVM)/clang-tidy --quiet --warnings-as-errors='*' --checks='$(TIDY_CHECKS)' \
		$(OWN) -- -std=c11 -I$(FILO)

check:
	cppcheck --enable=warning,style,performance,portability --inline-suppr \
		--suppress=missingIncludeSystem --error-exitcode=1 -I$(FILO) $(OWN)

# The core needs nothing from libc but the four memory functions.
freestanding:
	@mkdir -p build
	for f in gesso.c editor.c png.c gesso_c.c gessetto.c; do \
		$(LLVM)/clang --target=wasm32 -ffreestanding -nostdlib -fno-builtin -c -std=c11 -O2 \
			$(WARN) -isystem freestanding/include -I$(FILO) -o build/$${f%.c}_wasm.o $$f || exit 1; \
	done

qa: all fmt-check corpus editor png gessetto formats tidy check freestanding swift-fmt swift-build swift-test

clean:
	rm -rf build swift/.build swift/Frameworks
