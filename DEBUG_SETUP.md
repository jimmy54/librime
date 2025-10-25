# CLion Debug 配置说明

## 问题描述
在CLion的Debug模式下调试`rime_api_console`时,变量显示为"Summary Unavailable",特别是`std::string`类型的变量无法正常查看。

## 根本原因
1. **编译优化级别**:即使在Debug模式,如果没有显式设置编译标志,部分代码可能仍被优化
2. **调试信息不完整**:LLDB调试器需要完整的调试符号表才能正确解析标准库类型
3. **依赖库缺少调试信息**:可执行文件链接的所有库都需要包含调试信息

## 已完成的修改

### 1. 全局编译标志 (cmake/cxx_flag_overrides.cmake 和 c_flag_overrides.cmake)
为macOS/Linux平台的Debug模式添加了以下编译标志:
```cmake
-O0                      # 完全禁用优化
-g                       # 生成调试信息
-ggdb                    # 生成GDB/LLDB专用的调试信息
-fno-limit-debug-info    # 不限制调试信息的大小
-fno-omit-frame-pointer  # 保留栈帧指针,便于调试器追踪
-fstandalone-debug       # 生成独立的调试信息(对LLDB特别重要)
```

### 2. rime_api_console 可执行文件 (tools/CMakeLists.txt)
为`rime_api_console`添加了显式的Debug编译选项:
```cmake
target_compile_options(rime_api_console PRIVATE
  $<$<CONFIG:Debug>:-O0 -g -ggdb -fno-limit-debug-info -fno-omit-frame-pointer -fstandalone-debug>)
```

### 3. 所有 librime 库 (src/CMakeLists.txt)
为以下所有库添加了相同的Debug编译选项:
- `rime` (主库)
- `rime-dict` (词典库)
- `rime-gears` (齿轮库)
- `rime-levers` (杠杆库)
- `rime-plugins` (插件库)
- `rime-static` (静态库)

## 重新编译步骤

### 方法1: 在CLion中重新构建
1. 打开CLion
2. 选择菜单: **Tools → CMake → Reset Cache and Reload Project**
3. 等待CMake重新配置完成
4. 选择菜单: **Build → Rebuild Project**
5. 确保Build Configuration选择的是**Debug**模式

### 方法2: 在终端中手动编译
```bash
cd /Users/jimmy54/Pictures/jimmy_librime/librime

# 清理旧的构建文件
rm -rf build

# 重新配置(Debug模式)
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# 编译
cmake --build build -j$(sysctl -n hw.ncpu)
```

## CLion 调试器配置

### 1. 配置LLDB数据视图
打开 **Preferences → Build, Execution, Deployment → Debugger → Data Views**

勾选以下选项:
- ✅ **Enable alternative view for Collections classes**
- ✅ **Enable NatVis renderers for LLDB**

### 2. 配置Run/Debug Configuration
1. 打开 **Run → Edit Configurations**
2. 选择`rime_api_console`配置
3. 确保以下设置:
   - **Build Type**: Debug
   - **Target**: rime_api_console
   - **Executable**: build/bin/rime_api_console

## 调试时的临时解决方法

如果重新编译后仍然有个别变量显示"Summary Unavailable",可以使用以下方法:

### 方法A: 改变查看方式
在Variables窗口中:
1. 右键点击变量
2. 选择 **View as** → **Raw**

### 方法B: 使用LLDB Console
在调试时,打开LLDB Console(底部工具栏),输入以下命令:

```lldb
# 查看变量的完整内容
frame variable your_variable_name

# 或使用print命令
p your_variable_name

# 查看std::string的C字符串内容
p your_variable_name.c_str()

# 查看std::string的长度
p your_variable_name.size()

# 查看容器的元素数量
p your_vector.size()

# 查看容器的第一个元素
p your_vector[0]
```

### 方法C: 添加Watch表达式
在Variables窗口中:
1. 点击 **+** 按钮
2. 输入表达式,例如: `my_string.c_str()`
3. 这样可以直接看到字符串的内容

## 验证调试信息是否完整

编译完成后,可以使用以下命令验证调试信息:

```bash
# 检查可执行文件的调试符号
dsymutil -s build/bin/rime_api_console

# 检查库文件的调试符号
dsymutil -s build/lib/librime.1.14.0.dylib

# 使用nm查看符号表
nm -a build/bin/rime_api_console | grep debug
```

## 常见问题

### Q1: 重新编译后问题仍然存在?
**A**: 确保:
1. 完全清理了旧的build目录
2. CMake配置时明确指定了`-DCMAKE_BUILD_TYPE=Debug`
3. CLion的Build Configuration选择的是Debug模式

### Q2: 只有部分变量显示不正常?
**A**: 这可能是:
1. 变量被编译器优化掉了(即使在Debug模式)
2. 变量的作用域已经结束
3. 使用LLDB Console手动查看该变量

### Q3: 第三方库(Boost, glog等)的变量也显示不正常?
**A**: 这些库可能是Release模式编译的。可以考虑:
1. 使用Homebrew重新安装Debug版本: `brew install --build-from-source boost`
2. 或者使用LLDB Console查看这些变量的原始值

## 技术说明

### 为什么需要 -fstandalone-debug?
- 默认情况下,Clang会生成"分离"的调试信息,依赖外部符号表
- `-fstandalone-debug`强制将所有调试信息嵌入到二进制文件中
- 这对LLDB解析模板类(如`std::string`)特别重要

### 为什么需要 -fno-omit-frame-pointer?
- 帧指针用于追踪函数调用栈
- 优化时编译器可能省略帧指针以提高性能
- 保留帧指针可以让调试器更准确地显示调用栈和局部变量

### 为什么所有库都需要调试信息?
- 调试器需要完整的符号表来解析变量类型
- 如果某个库缺少调试信息,该库中定义的类型就无法正确显示
- 例如,如果`librime.dylib`没有调试信息,其中定义的所有类的成员变量都无法查看

## 参考资料
- [LLDB Debugging Guide](https://lldb.llvm.org/use/tutorial.html)
- [CMake Debug Configuration](https://cmake.org/cmake/help/latest/variable/CMAKE_BUILD_TYPE.html)
- [Clang Debug Options](https://clang.llvm.org/docs/CommandGuide/clang.html#debug-options)
