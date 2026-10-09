# 嘉立创EDA（EasyEDA）与 VS Code GitHub Copilot 环境搭建

本文只介绍如何搭建环境。完成后，GitHub Copilot Agent 就可以通过 `easyeda-api` Skill 连接嘉立创EDA。

## 第 1 步：安装必要软件

安装以下软件：

1. [嘉立创EDA专业版](https://pro.easyeda.com/)
2. [Visual Studio Code](https://code.visualstudio.com/)
3. [Node.js 18 或更高版本](https://nodejs.org/)
4. [Git for Windows](https://git-scm.com/download/win)

打开 PowerShell，检查 Node.js、npm 和 Git：

```powershell
node -v
npm -v
git --version
```

三个命令都能显示版本号，说明安装成功。

## 第 2 步：安装 VS Code 扩展

在 VS Code 的扩展商店中安装：

1. `GitHub Copilot`
2. `GitHub Copilot Chat`

安装后，使用具有 GitHub Copilot 授权的 GitHub 账号登录。

## 第 3 步：安装 easyeda-api Skill

打开 PowerShell，执行：

```powershell
New-Item -ItemType Directory -Force "$env:USERPROFILE\.copilot\skills"
git clone https://github.com/easyeda/easyeda-api-skill.git "$env:USERPROFILE\.copilot\skills\easyeda-api"
Set-Location "$env:USERPROFILE\.copilot\skills\easyeda-api"
npm install
```

检查 Skill 文件是否存在：

```powershell
Test-Path "$env:USERPROFILE\.copilot\skills\easyeda-api\SKILL.md"
```

输出 `True` 表示安装成功。

## 第 4 步：安装 Run API Gateway

1. 打开嘉立创EDA专业版。
2. 打开“扩展管理器”。
3. 搜索并安装 `Run API Gateway`。
4. 如果扩展管理器中找不到，请从 [Run API Gateway 扩展页面](https://jlc-ext.com/item/oshwhub/run-api-gateway) 下载并安装。
5. 在扩展设置中启用“允许外部交互”和“显示在顶部菜单”。

安装成功后，顶部菜单中会出现 `API Gateway`。

## 第 5 步：让 VS Code 识别 Skill

1. 在 VS Code 中按 `Ctrl+Shift+P`。
2. 运行 `Developer: Reload Window`。
3. 打开 Copilot Chat。
4. 输入 `/skills`。
5. 确认列表中存在 `easyeda-api`。
6. 将 Copilot Chat 模式切换为 `Agent`。

## 第 6 步：启动连接

每次使用时，按以下顺序操作：

1. 打开嘉立创EDA专业版。
2. 打开需要使用的工程、原理图或 PCB。
3. 打开 PowerShell，执行：

```powershell
Set-Location "$env:USERPROFILE\.copilot\skills\easyeda-api"
npm.cmd run server
```

4. 保持这个 PowerShell 窗口运行。
5. 回到嘉立创EDA，选择 `API Gateway → Reconnect`。
6. 打开 `API Gateway → About...`，确认状态为 `Connected`。

Bridge Server 默认使用 `49620` 端口。如果该端口被占用，会自动尝试 `49621-49629`。

## 第 7 步：检查连接

重新打开一个 PowerShell 窗口，执行：

```powershell
Invoke-RestMethod http://127.0.0.1:49620/health |
    ConvertTo-Json -Depth 5
```

如果 Bridge 使用的不是 `49620`，请将命令中的端口改为 `API Gateway → About...` 显示的端口。

连接成功时，结果中应包含：

```json
{
  "service": "easyeda-bridge",
  "status": "ok",
  "edaConnected": true,
  "edaWindowCount": 1
}
```

重点确认：

- `status` 是 `ok`
- `edaConnected` 是 `true`
- `edaWindowCount` 大于或等于 `1`

## 第 8 步：在 Copilot 中进行最终测试

在 VS Code Copilot Chat 的 Agent 模式中输入：

```text
使用 easyeda-api skill 检查当前环境。
告诉我 Bridge 是否正常、是否已连接嘉立创EDA，以及当前连接的窗口数量。
不要修改任何工程数据。
```

Copilot 能返回连接状态和窗口数量，说明环境搭建完成。

## 日常使用方法

每次电脑重启后，按以下顺序启动：

1. 打开嘉立创EDA专业版和需要操作的工程。
2. 打开 PowerShell，执行：

```powershell
Set-Location "$env:USERPROFILE\.copilot\skills\easyeda-api"
npm.cmd run server
```

3. 保持这个 PowerShell 窗口运行，不要关闭，也不要按 `Ctrl+C`。
4. 在嘉立创EDA中选择 `API Gateway → Reconnect`。
5. 打开 VS Code，将 Copilot Chat 切换为 `Agent` 模式后即可使用。

注意：

- 正确命令是 `npm.cmd run server`，不是 `npm.com run server`。
- Bridge Server 每次只需启动一次，不需要重复输入命令。
- 只要 PowerShell 窗口保持运行，Bridge Server 就会一直工作。
- 关闭 PowerShell、按下 `Ctrl+C` 或重启电脑后，下次使用前需要重新启动 Bridge Server。
- 使用结束后，可以在 Bridge Server 的 PowerShell 窗口按 `Ctrl+C` 停止服务。