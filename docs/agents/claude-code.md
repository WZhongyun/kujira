# Claude Code 接入

## 原理

Claude Code 提供官方的 [hooks](https://code.claude.com/docs/en/hooks) 机制：在指定事件发生时，把事件内容（JSON）发给一个命令或 HTTP 地址。鲸鱼娘在本机 `127.0.0.1:38111` 监听，收到事件后立刻回复一个空的 JSON 对象 `{}`，再把事件放进队列交给动画处理。

- **只观察，不干预**：回复里不带任何决策字段，不会阻止、修改或批准任何工具调用。
- **不拖慢 Claude Code**：HTTP hook 超时设为 2 秒；服务端收到请求后立即返回。
- **鲸鱼娘没运行时**：连接失败属于非阻塞错误，Claude Code 照常工作。
- **只监听本机**：只绑定 127.0.0.1，并校验请求头里的随机令牌 `X-Kujira-Token`，防止本机其他程序伪造事件。

## 安装位置

写入 Claude Code 的用户级配置 `~/.claude/settings.json`（Windows 上是 `%USERPROFILE%\.claude\settings.json`；设置了 `CLAUDE_CONFIG_DIR` 时用那个目录）。用户级 hook 在所有项目里生效，Claude Code 会自动重新加载，不需要重启。

注册的事件：`SessionStart`、`SessionEnd`、`UserPromptSubmit`、`PreToolUse`、`PostToolUse`、`Notification`、`Stop`。只用了长期存在的事件名，避免旧版本不认识。

## 两种接入方式

**HTTP hook（默认，推荐）**，每个事件追加一条：

```json
{
  "hooks": [
    {
      "type": "http",
      "url": "http://127.0.0.1:38111/v1/events/claude-code",
      "timeout": 2,
      "headers": { "X-Kujira-Token": "<随机令牌>" }
    }
  ]
}
```

**命令 hook + curl（兼容旧版本）**：如果你的 Claude Code 版本还不支持 `type: "http"`，在设置里切换到这个方式。它以异步方式运行 curl（`async: true`），不会让工具调用等待；命令末尾的 `|| true` 保证退出码永远是 0，不会被当作阻塞错误。

## 安装器如何保护你的配置

1. **先解析，再动手**：文件不是合法 JSON、含有注释、或 `hooks` 不是对象时，拒绝修改并提示原因。
2. **每次修改前备份**：同目录生成 `settings.json.kujira-backup-年月日-时分秒`，保留最近 5 份。
3. **只动自己的条目**：只识别指向 `127.0.0.1` 且路径是 `/v1/events/claude-code` 的 hook。你原有的 hook、权限、模型等设置原样保留，键的顺序也不变。重复安装会先删掉旧条目再加新的，不会越积越多。
4. **原子写入**：先写临时文件再重命名覆盖，写到一半断电也不会留下损坏的文件。
5. **写后校验**：写完重新读取，确认状态为「已安装」。
6. **卸载**：只删除自己的条目；因此变空的事件列表会一起删除，其他内容不动。

唯一的可见变化：文件会以 2 空格缩进重新排版。

## 手动测试

鲸鱼娘运行时，在终端里发一个假事件（把令牌换成 `config.json` 里的 `token`）：

```bash
curl -X POST http://127.0.0.1:38111/v1/events/claude-code \
  -H "Content-Type: application/json" -H "X-Kujira-Token: <token>" \
  -d '{"session_id":"test","hook_event_name":"PreToolUse","tool_name":"Edit"}'
```

她应该拿起画笔。`GET http://127.0.0.1:38111/v1/health` 可以确认服务是否在运行。
