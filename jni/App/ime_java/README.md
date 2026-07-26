# 输入法辅助 DEX（`com.mxp.Helper`）源码与重建说明

`jni/App/ime_dex_data.h` 里的 `imgui_dex[]` 是这份 DEX 的字节数组，运行时由
`ImeLoadDex()`（`jni/res/输入法桥接.cpp`）用 `InMemoryDexClassLoader` 内存加载，
不落盘。原始 `.java` 工程文件已不在仓库里，这里的源码是从 DEX 反编译恢复的。

## 目录内容

| 文件 | 说明 |
|---|---|
| `smali/com/mxp/*.smali` | **权威可编辑形式**。改动请直接改 smali，再按下面的流程打回 DEX。 |
| `Helper.decompiled.java` | CFR 反编译出的 Java，仅供阅读理解逻辑，**不能直接 javac**（缺 `android.jar`，且反编译产物含 `** GOTO` 之类无法编译的结构）。 |
| `GLES3JNIView.decompiled.java` | 同上。 |
| `imgui_dex.dex` | 当前 `ime_dex_data.h` 对应的 DEX 原文件，便于校验。 |

## `Helper` 对外接口（native 侧通过 JNI 调用）

```
static void init(Activity)              // 保存 Activity 引用
static void showSoftKeyboard(boolean)   // 显示/隐藏软键盘
static void showToast(String)
static void hideScreen(boolean)         // FLAG_SECURE 防截屏
static void IsVpnActive(boolean)        // VPN 检测
static native void nativeAddChar(int)   // 字符回传 ImGui（由 native RegisterNatives 绑定）
static native void nativeKeyEvent(int)  // 功能键回传（67=退格 66=回车 21/22=左右 112=删除）
```

键盘输入的实现方式：往 DecorView 挂一个 `alpha=0` 的 1x1 `EditText`，内容恒为
8 个空格的哨兵串（`KB_SENTINEL`）。`TextWatcher` 对比文本与哨兵的差异，推断出
用户输入了什么字符或按了退格，再通过 `nativeAddChar` / `nativeKeyEvent` 送进 ImGui，
最后把文本复位成哨兵串。

## 重建流程（dex ↔ smali）

d8/dx 在 Google 的 maven 上，本环境不可达；smali/baksmali 在 Maven Central 上可获取，
因此走 smali 路线，无需 `android.jar`，也不需要 NDK。

```bash
# 取工具（一次即可）
mvn dependency:copy-dependencies -DoutputDirectory=libs   # org.smali:smali:2.5.2
                                                          # org.smali:baksmali:2.5.2
                                                          # de.femtopedia.dex2jar:dex2jar:2.4.38（校验用）
                                                          # org.benf:cfr:0.152（校验用）

# 1) DEX → smali
java -cp "libs/*" org.jf.baksmali.Main d imgui_dex.dex -o smali

# 2) 改 smali

# 3) smali → DEX
java -cp "libs/*" org.jf.smali.Main a smali -o imgui_dex.dex --api 26

# 4) 校验：反编译回 Java，确认只改了预期的地方
java -cp "libs/*" com.googlecode.dex2jar.tools.Dex2jarCmd imgui_dex.dex -o out.jar -f
java -jar libs/cfr-0.152.jar out.jar --outputdir out_src

# 5) DEX → C 头文件数组
python3 - <<'EOF'
d = open("imgui_dex.dex", "rb").read()
lines = ["unsigned char imgui_dex[] = {"]
for i in range(0, len(d), 12):
    chunk = d[i:i+12]
    lines.append("  " + ", ".join(f"0x{b:02x}" for b in chunk) + ("," if i + 12 < len(d) else ""))
lines += ["};", f"unsigned int imgui_dex_len = {len(d)};"]
open("../ime_dex_data.h", "w").write("\n".join(lines) + "\n")
EOF
```

`InMemoryDexClassLoader` 会校验 DEX 头部的 adler32 与 SHA-1，smali 汇编时会自动写对；
改完可以用下面的脚本确认，不匹配的话运行时会直接拒绝加载：

```python
import zlib, hashlib, struct
d = open("imgui_dex.dex", "rb").read()
assert struct.unpack_from("<I", d, 8)[0] == zlib.adler32(d[12:]) & 0xffffffff
assert d[12:32] == hashlib.sha1(d[32:]).digest()
assert struct.unpack_from("<I", d, 32)[0] == len(d)
```

## 已做的改动

`lambda$showSoftKeyboard$3` 里的 `keyboardActive` 闩锁原本会把整个显示逻辑跳过：

```java
if (!keyboardActive) {
    ...重置哨兵文本...
    keyboardEditText.requestFocus();
    keyboardImm.showSoftInput(keyboardEditText, 2);
    keyboardActive = true;
}
```

用户按系统返回键收起键盘时，Android 把输入法关掉了，但没有任何回调会把
`keyboardActive` 置回 false，于是它一直是 true。之后 native 再怎么调
`showSoftKeyboard(true)` 都会被这个闩锁吞掉，键盘再也弹不出来。

改成：

```java
if (!keyboardActive) {
    ...重置哨兵文本...        // 仍然只在真正从关闭状态启动时做
}
keyboardEditText.requestFocus();          // 每次都重新抢焦点
keyboardImm.showSoftInput(keyboardEditText, 2);   // 每次都重新请求显示
keyboardActive = true;
```

哨兵文本的重置保持在 `!keyboardActive` 内，是为了不打断中文输入法正在进行的
拼音组合；`requestFocus` / `showSoftInput` 在键盘已显示时是无副作用的空操作。

对应 smali 改动（`Helper.smali`）：`if-nez p0, :cond_d7` 改为跳到新增的
`:cond_reassert` 标签，该标签插在 `requestFocus` 之前。
