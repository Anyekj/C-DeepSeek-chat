# C-DeepSeek-chat
基于C语言实现的调用DeepSeek api，实现了多轮对话(上下文拼接)，显示R1推理过程，流式输出，等基础功能

# DeepSeek 终端聊天工具
一个基于DeepSeek API的终端对话工具，支持流式响应和深度搜索模式，提供实时的思考过程展示。

# DeepSeek 官网
https://www.deepseek.com/

# api
官方文档：https://api-docs.deepseek.com/zh-cn/

官方api：https://platform.deepseek.com/

## 主要特性

- 🌊 **流式响应** - 实时显示AI的思考过程和回答内容
- 🔍 **双模式切换** - 支持普通聊天/深度搜索模式即时切换
- 🎨 **彩色高亮** - 黄色显示思考过程，绿色显示最终回答
- 🔑 **API集成** - 轻松对接DeepSeek官方API
- 💻 **跨平台** - 支持Linux/macOS终端环境

## 快速开始

### 依赖安装
```bash
# Ubuntu/Debian
sudo apt-get install libcurl4-openssl-dev

# CentOS/RHEL
sudo yum install libcurl-devel

# macOS (使用Homebrew)
brew install curl

#反正就是curl库
