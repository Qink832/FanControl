# 目标可执行文件名称
TARGET1 := AutoFanCtrl
TARGET2 := ManFanCtrl

# 源文件列表
SRCS1 := AutoFanControl.cpp FanController.cpp
SRCS2 := ManualFanControl.cpp

# C++ 编译器
CXX := g++

# 编译选项
CXXFLAGS := -w -std=c++17 -I/usr/local/Ascend/driver/include

# 链接选项
LDFLAGS := -L/usr/local/Ascend/driver/lib64/driver

# 需要链接的库
LIBS := -ldcmi -lpthread

# 安装路径
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
SYSTEMDDIR1 = /lib/systemd/system
SYSTEMDDIR2 = /etc/systemd/system

# systemd服务文件
SERVICE_FILE := autofanctrl.service

# 默认目标
all: $(TARGET1) $(TARGET2)

# 规则：生成可执行文件
$(TARGET1): $(SRCS1)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) $(LIBS) -o $@

$(TARGET2): $(SRCS2)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) $(LIBS) -o $@

# 清理生成的文件
clean:
	rm -f $(TARGET1) $(TARGET2)

# 安装规则
install:
	mkdir -p $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET1) $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET2) $(DESTDIR)$(BINDIR)
	install -m 644 $(SERVICE_FILE) $(DESTDIR)$(SYSTEMDDIR1)
	install -m 644 $(SERVICE_FILE) $(DESTDIR)$(SYSTEMDDIR2)

# 卸载规则
uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET1)
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET2)
	rm -f $(DESTDIR)$(SYSTEMDDIR1)/$(SERVICE_FILE)
	rm -f $(DESTDIR)$(SYSTEMDDIR2)/$(SERVICE_FILE)
	rm -f /etc/FanControlParams.json

.PHONY: all clean install uninstall