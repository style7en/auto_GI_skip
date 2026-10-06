# BetterYuanshen —— MinGW (g++) 构建脚本
#
# 用法：
#   make              构建正式版 -> release/BetterYuanshen.exe（requireAdministrator）
#   make dev          构建开发版 -> build/dev/BetterYuanshen.exe（asInvoker，可非管理员运行）
#   make clean        清理全部构建产物
#   make rebuild      清理后重新构建
#   make cv-test      运行视觉算法自检
#   make capture-test 运行截图管线自检
#   make ui-preview   离屏渲染界面到 docs/ui-preview.png
#
# 目录约定（项目根目录只保留 build/ 与 release/ 两个生成目录）：
#   build/obj/    正式版中间产物（.o / .res）
#   build/dev/    开发版中间产物与开发版 exe
#   release/      正式版 exe 与 assets（唯一的分发目录）

# ---------------------------------------------------------------------------
# 工具
# ---------------------------------------------------------------------------

CXX     := g++
WINDRES := windres

# 源码与输出
SRC_DIR := src
RES_DIR := resources
ASSETS  := assets

# 模式：release（默认）或 dev
#
# 目录约定（根目录只保留 build/ 与 release/ 两项）：
#   build/           正式版中间产物
#   build/dev/       开发版中间产物 + 开发版 exe
#   release/         正式版 exe + assets（唯一的分发目录）
MODE ?= release

ifeq ($(MODE),dev)
  RC_FILE := $(RES_DIR)/app.dev.rc
  OBJ_DIR := build/dev/obj
  OUT_DIR := build/dev
else
  RC_FILE := $(RES_DIR)/app.rc
  OBJ_DIR := build/obj
  OUT_DIR := release
endif

TARGET := $(OUT_DIR)/BetterYuanshen.exe

# ---------------------------------------------------------------------------
# 编译 / 链接参数
# ---------------------------------------------------------------------------

# -municode       入口为 wWinMain（MinGW 必需，否则报 undefined reference to WinMain）
# -finput/exec-charset  源文件与运行时字符串统一 UTF-8
DEFINES := \
	-DUNICODE -D_UNICODE \
	-DWIN32_LEAN_AND_MEAN -DNOMINMAX \
	-D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00

INCLUDES := -I$(SRC_DIR) -I.

CXXFLAGS := -std=c++20 -O2 -municode \
	-Wall -Wextra -Wno-unused-parameter \
	-finput-charset=UTF-8 -fexec-charset=UTF-8 \
	$(DEFINES) $(INCLUDES)

# -mwindows             GUI 子系统（不弹控制台）
# -static-libgcc/stdc++ 静态链接运行时，得到真正单文件
LDFLAGS := -municode -mwindows -static -static-libgcc -static-libstdc++

LIBS := \
	-luser32 -lgdi32 -lshell32 -lcomctl32 \
	-ld3d11 -ldxgi -ldxguid \
	-lole32 -loleaut32 -luuid \
	-lwindowscodecs

# ---------------------------------------------------------------------------
# 源文件
# ---------------------------------------------------------------------------

SRCS := $(wildcard $(SRC_DIR)/*.cpp) \
        $(wildcard $(SRC_DIR)/*/*.cpp)

OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))
RES  := $(OBJ_DIR)/app.res

# 头文件变化触发重编译
DEPS := $(wildcard $(SRC_DIR)/*.h) \
        $(wildcard $(SRC_DIR)/*/*.h) \
        $(wildcard $(RES_DIR)/*.h)

# ---------------------------------------------------------------------------
# 目标
# ---------------------------------------------------------------------------

.PHONY: all dev clean rebuild run cv-test capture-test ui-preview help

all: $(TARGET)

dev:
	@$(MAKE) MODE=dev all

$(TARGET): $(OBJS) $(RES)
	@mkdir -p $(OUT_DIR)
	$(CXX) $(LDFLAGS) $(OBJS) $(RES) -o $@ $(LIBS)
	@mkdir -p $(OUT_DIR)/$(ASSETS)
	@cp -r $(ASSETS)/. $(OUT_DIR)/$(ASSETS)/
	@echo ""
	@echo "  [OK] $@"
	@ls -l $@ | awk '{printf "  size: %.1f KB\n", $$5/1024}'

# 编译单个源文件（自动建子目录）
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp $(DEPS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 编译资源（图标 + 清单 + 版本信息 + 内嵌模板）
# --codepage=65001 让 windres 按 UTF-8 解析源文件
$(RES): $(RC_FILE) $(RES_DIR)/resource.h $(RES_DIR)/templates.rcinc \
        $(RES_DIR)/version.rcinc $(RES_DIR)/app.manifest $(RES_DIR)/app.dev.manifest \
        $(RES_DIR)/app.ico $(wildcard $(ASSETS)/1920x1080/*.png)
	@mkdir -p $(OBJ_DIR)
	$(WINDRES) --codepage=65001 -I$(RES_DIR) -I$(RES_DIR)/.. $< -O coff -o $@

clean:
	@rm -rf build release build-dev release-dev
	@echo "  [OK] cleaned"

rebuild: clean all

# 自检与预览都走开发版（asInvoker）。
# 正式版嵌入的是 requireAdministrator 清单，在普通终端里会被系统拒绝启动。
DEV_TARGET := build/dev/BetterYuanshen.exe

run:
	@$(MAKE) --no-print-directory MODE=dev all
	@./$(DEV_TARGET)

cv-test:
	@$(MAKE) --no-print-directory MODE=dev all
	@mkdir -p build/dev/out
	@./$(DEV_TARGET) --cv-test build/dev/out
	@cat build/dev/out/cv-report.txt

capture-test:
	@$(MAKE) --no-print-directory MODE=dev all
	@mkdir -p build/dev/out
	@./$(DEV_TARGET) --capture-test build/dev/out
	@cat build/dev/out/capture-report.txt

ui-preview:
	@$(MAKE) --no-print-directory MODE=dev all
	@mkdir -p docs
	@./$(DEV_TARGET) --render-ui docs/ui-preview.png
	@echo "  [OK] docs/ui-preview.png"

help:
	@echo "Targets:"
	@echo "  make              build release  -> release/"
	@echo "  make dev          build dev      -> build/dev/"
	@echo "  make clean        remove build outputs"
	@echo "  make rebuild      clean + build"
	@echo "  make cv-test      vision algorithm self-test"
	@echo "  make capture-test capture pipeline self-test"
	@echo "  make ui-preview   render UI to docs/ui-preview.png"
