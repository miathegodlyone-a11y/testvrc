# zygisk-vrchat-test

Zygisk module that injects into VRChat (`com.vrchat.mobile.standalone`) on Quest 3 via Singularity-Magisk.

## Build

**Requirements:** Android NDK (r25+), CMake 3.18+

```bash
# Set NDK path if not auto-detected
export ANDROID_NDK_HOME=/path/to/ndk

chmod +x build.sh && ./build.sh
# → release/zygisk-vrchat-test.zip
```

## Install

Flash `release/zygisk-vrchat-test.zip` through **Singularity-Magisk** module installer,
or via ADB:

```bash
adb push release/zygisk-vrchat-test.zip /sdcard/
adb shell su -c "magisk --install-module /sdcard/zygisk-vrchat-test.zip"
# Then reboot headset
adb reboot
```

## Verify it loaded

```bash
adb logcat -s ZygiskVRC
```

You should see:
```
I ZygiskVRC: preAppSpecialize: VRChat process matched, staying loaded
I ZygiskVRC: postAppSpecialize: running inside VRChat — hook here
I ZygiskVRC: postAppSpecialize: done
```

## Adding hooks

Edit `src/main.cpp` → `postAppSpecialize()`. The comments show three approaches:
- **shadowhook/dobby** inline hook on `libil2cpp.so`
- **JNI method replacement**
- **Offset-based hook** via `/proc/self/maps`

Rebuild and reflash after changes.

## Notes

- `FORCE_DENYLIST_UNMOUNT` keeps module files hidden from VRChat's own file checks
- `DLCLOSE_MODULE_LIBRARY` on non-VRChat processes — don't waste RAM
- Tested against Singularity v1.0.6.1 fugu exploit on Quest 3
