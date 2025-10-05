# iOS 平台的第三方库编译配置
# 基于 deps.mk,添加了 iOS 工具链支持

rime_root = $(CURDIR)
src_dir = $(rime_root)/deps

ifndef NOPARALLEL
export MAKEFLAGS+=" -j$$(( $$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || getconf NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 8) + 1)) "
endif

build ?= build-ios
prefix ?= $(rime_root)

# iOS 工具链配置
ios_cmake_toolchain ?= $(rime_root)/../ios-cmake/ios.toolchain.cmake
ios_platform ?= OS64
ios_deployment_target ?= 13.0

# 检查工具链文件是否存在
ifeq (,$(wildcard $(ios_cmake_toolchain)))
$(error iOS toolchain file not found: $(ios_cmake_toolchain))
endif

# 通用的 iOS CMake 参数
ios_cmake_flags = \
	-DCMAKE_TOOLCHAIN_FILE=$(ios_cmake_toolchain) \
	-DPLATFORM=$(ios_platform) \
	-DDEPLOYMENT_TARGET=$(ios_deployment_target) \
	-G Xcode

rime_deps = glog googletest leveldb marisa-trie opencc yaml-cpp

.PHONY: all clean clean-dist clean-src $(rime_deps) info

all: info $(rime_deps)

info:
	@echo "=========================================="
	@echo "iOS 依赖库编译配置"
	@echo "=========================================="
	@echo "工具链文件: $(ios_cmake_toolchain)"
	@echo "目标平台: $(ios_platform)"
	@echo "最低 iOS 版本: $(ios_deployment_target)"
	@echo "构建目录: $(build)"
	@echo "安装前缀: $(prefix)"
	@echo "依赖库列表: $(rime_deps)"
	@echo "=========================================="
	@echo ""

clean: clean-src clean-dist

clean-dist:
	git rev-parse --is-inside-work-tree > /dev/null && \
	find $(prefix)/bin $(prefix)/include $(prefix)/lib $(prefix)/share \
	-depth -maxdepth 1 \
	-exec bash -c 'git ls-files --error-unmatch "$$0" > /dev/null 2>&1 || rm -rv "$$0"' {} \; || true
	rmdir $(prefix) 2> /dev/null || true

# 清理 iOS 构建目录
clean-src:
	@echo "清理 iOS 构建目录..."
	for dep in $(rime_deps); do \
		rm -rf $(src_dir)/$${dep}/$(build) || true; \
	done

# glog - Google 日志库
glog:
	@echo "=========================================="
	@echo "编译 glog for iOS..."
	@echo "=========================================="
	cd $(src_dir)/glog; \
	cmake . -B$(build) \
	$(ios_cmake_flags) \
	-DBUILD_SHARED_LIBS:BOOL=OFF \
	-DBUILD_TESTING:BOOL=OFF \
	-DWITH_GFLAGS:BOOL=OFF \
	-DCMAKE_BUILD_TYPE:STRING="Release" \
	-DCMAKE_INSTALL_PREFIX:PATH="$(prefix)" \
	&& cmake --build $(build) --config Release --target install

# googletest - Google 测试框架
googletest:
	@echo "=========================================="
	@echo "编译 googletest for iOS..."
	@echo "=========================================="
	cd $(src_dir)/googletest; \
	cmake . -B$(build) \
	$(ios_cmake_flags) \
	-DBUILD_GMOCK:BOOL=OFF \
	-DCMAKE_BUILD_TYPE:STRING="Release" \
	-DCMAKE_INSTALL_PREFIX:PATH="$(prefix)" \
	&& cmake --build $(build) --config Release --target install

# leveldb - 键值存储数据库
leveldb:
	@echo "=========================================="
	@echo "编译 leveldb for iOS..."
	@echo "=========================================="
	cd $(src_dir)/leveldb; \
	cmake . -B$(build) \
	$(ios_cmake_flags) \
	-DLEVELDB_BUILD_BENCHMARKS:BOOL=OFF \
	-DLEVELDB_BUILD_TESTS:BOOL=OFF \
	-DHAVE_CRC32C:BOOL=OFF \
	-DHAVE_SNAPPY:BOOL=OFF \
	-DHAVE_TCMALLOC:BOOL=OFF \
	-DCMAKE_CXX_FLAGS="-Wno-error=shorten-64-to-32 -Wno-shorten-64-to-32" \
	-DCMAKE_BUILD_TYPE:STRING="Release" \
	-DCMAKE_INSTALL_PREFIX:PATH="$(prefix)" \
	&& cmake --build $(build) --config Release --target install

# marisa-trie - Trie 树数据结构
marisa-trie:
	@echo "=========================================="
	@echo "编译 marisa-trie for iOS..."
	@echo "=========================================="
	cd $(src_dir)/marisa-trie; \
	cmake . -B$(build) \
	$(ios_cmake_flags) \
	-DCMAKE_BUILD_TYPE:STRING="Release" \
	-DCMAKE_INSTALL_PREFIX:PATH="$(prefix)" \
	-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON \
	-DBUILD_TESTING:BOOL=OFF \
	-DENABLE_TOOLS:BOOL=OFF \
	&& cmake --build $(build) --config Release --target install

# opencc - 简繁体转换库
opencc:
	@echo "=========================================="
	@echo "编译 opencc for iOS..."
	@echo "=========================================="
	@echo "注意: 临时修改 CMakeLists.txt 以跳过工具和数据编译"
	cd $(src_dir)/opencc; \
	if [ -f CMakeLists.txt.bak ]; then \
		mv CMakeLists.txt.bak CMakeLists.txt; \
	fi; \
	cp CMakeLists.txt CMakeLists.txt.bak; \
	sed -i.tmp 's/add_subdirectory(src)/#add_subdirectory(src)/' CMakeLists.txt; \
	sed -i.tmp 's/add_subdirectory(data)/#add_subdirectory(data)/' CMakeLists.txt; \
	echo 'add_subdirectory(src)' >> CMakeLists.txt; \
	rm -f CMakeLists.txt.tmp; \
	cd src; \
	if [ -f CMakeLists.txt.bak2 ]; then \
		mv CMakeLists.txt.bak2 CMakeLists.txt; \
	fi; \
	cp CMakeLists.txt CMakeLists.txt.bak2; \
	sed -i.tmp 's/add_subdirectory(tools)/#add_subdirectory(tools)/' CMakeLists.txt; \
	rm -f CMakeLists.txt.tmp; \
	cd ..; \
	cmake . -B$(build) \
	$(ios_cmake_flags) \
	-DBUILD_SHARED_LIBS:BOOL=OFF \
	-DBUILD_DOCUMENTATION:BOOL=OFF \
	-DBUILD_TESTING:BOOL=OFF \
	-DENABLE_BENCHMARK:BOOL=OFF \
	-DUSE_SYSTEM_MARISA:BOOL=OFF \
	-DCMAKE_BUILD_TYPE:STRING="Release" \
	-DCMAKE_INSTALL_PREFIX:PATH="$(prefix)" \
	-Wno-dev \
	&& cmake --build $(build) --config Release --target libopencc \
	&& cmake --build $(build) --config Release --target install \
	&& mv CMakeLists.txt.bak CMakeLists.txt \
	&& cd src && mv CMakeLists.txt.bak2 CMakeLists.txt

# yaml-cpp - YAML 解析库
yaml-cpp:
	@echo "=========================================="
	@echo "编译 yaml-cpp for iOS..."
	@echo "=========================================="
	cd $(src_dir)/yaml-cpp; \
	cmake . -B$(build) \
	$(ios_cmake_flags) \
	-DYAML_CPP_BUILD_CONTRIB:BOOL=OFF \
	-DYAML_CPP_BUILD_TESTS:BOOL=OFF \
	-DYAML_CPP_BUILD_TOOLS:BOOL=OFF \
	-DCMAKE_BUILD_TYPE:STRING="Release" \
	-DCMAKE_INSTALL_PREFIX:PATH="$(prefix)" \
	&& cmake --build $(build) --config Release --target install

# 辅助目标 - 编译特定平台
.PHONY: device simulator simulator-arm64

# iOS 真机 (arm64)
device:
	$(MAKE) -f deps-ios.mk all ios_platform=OS64

# iOS 模拟器 (x86_64, Intel Mac)
simulator:
	$(MAKE) -f deps-ios.mk all ios_platform=SIMULATOR64

# iOS 模拟器 (arm64, Apple Silicon Mac)
simulator-arm64:
	$(MAKE) -f deps-ios.mk all ios_platform=SIMULATORARM64

# 帮助信息
.PHONY: help
help:
	@echo "iOS 依赖库编译 Makefile"
	@echo ""
	@echo "用法:"
	@echo "  make -f deps-ios.mk [target] [options]"
	@echo ""
	@echo "目标 (Targets):"
	@echo "  all              - 编译所有依赖库 (默认)"
	@echo "  glog             - 仅编译 glog"
	@echo "  googletest       - 仅编译 googletest"
	@echo "  leveldb          - 仅编译 leveldb"
	@echo "  marisa-trie      - 仅编译 marisa-trie"
	@echo "  opencc           - 仅编译 opencc"
	@echo "  yaml-cpp         - 仅编译 yaml-cpp"
	@echo "  device           - 编译 iOS 真机版本 (arm64)"
	@echo "  simulator        - 编译 iOS 模拟器版本 (x86_64)"
	@echo "  simulator-arm64  - 编译 iOS 模拟器版本 (arm64)"
	@echo "  clean            - 清理构建文件"
	@echo "  info             - 显示配置信息"
	@echo "  help             - 显示此帮助信息"
	@echo ""
	@echo "选项 (Options):"
	@echo "  ios_platform=<PLATFORM>           - 设置 iOS 平台"
	@echo "    可选值: OS64, SIMULATOR64, SIMULATORARM64, OS64COMBINED"
	@echo "  ios_deployment_target=<VERSION>   - 设置最低 iOS 版本 (默认: 13.0)"
	@echo "  ios_cmake_toolchain=<PATH>        - 设置工具链文件路径"
	@echo "  prefix=<PATH>                     - 设置安装路径 (默认: 当前目录)"
	@echo ""
	@echo "示例:"
	@echo "  make -f deps-ios.mk                              # 编译所有库 (真机)"
	@echo "  make -f deps-ios.mk simulator-arm64              # 编译模拟器版本"
	@echo "  make -f deps-ios.mk leveldb                      # 仅编译 leveldb"
	@echo "  make -f deps-ios.mk ios_platform=SIMULATOR64    # 指定平台编译"
	@echo "  make -f deps-ios.mk clean                        # 清理构建文件"
	@echo ""
