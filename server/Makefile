# 网盘服务器 Makefile
# SQLite 用 gcc 编译 (C 库), C++ 代码用 g++ 编译

CC = /c/mingw64/mingw64/bin/gcc.exe
CXX = /c/mingw64/mingw64/bin/g++.exe
RC = /c/mingw64/mingw64/bin/windres.exe

CFLAGS = -O2 -I/c/mingw64/mingw64/x86_64-w64-mingw32/include
CXXFLAGS = -std=c++17 -O2 -Wall -Iinclude -Ilib \
           -I/c/mingw64/mingw64/opt/include \
           -I/c/mingw64/mingw64/x86_64-w64-mingw32/include

LDFLAGS = -L/c/mingw64/mingw64/opt/lib \
          -L/c/mingw64/mingw64/x86_64-w64-mingw32/lib \
          -lssl -lcrypto -lz -lws2_32 -lwinmm -lcomctl32 -lgdi32 -static -static-libgcc -static-libstdc++ -mwindows

# 源文件
SERVER_SRCS = src/database.cpp src/storage.cpp src/http_server.cpp src/server.cpp src/main.cpp

# 目标
TARGET = ../cloud_server.exe

.PHONY: all clean

all: $(TARGET)

# SQLite 用 gcc 编译
lib/sqlite3.o: lib/sqlite3.c
	$(CC) $(CFLAGS) -c $< -o $@

# 资源文件 (嵌入高 DPI 清单)
res/app_res.o: res/app.rc res/app.manifest
	$(RC) -O coff $< -o $@

# C++ 源文件编译
src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 链接
$(TARGET): $(SERVER_SRCS:src/%.cpp=src/%.o) lib/sqlite3.o res/app_res.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "构建完成: $@"

clean:
	rm -f src/*.o lib/sqlite3.o res/app_res.o $(TARGET)
