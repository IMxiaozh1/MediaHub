# 内置浏览器选项卡、历史与收藏夹实施计划

> 依据：`docs/设计/06-内置浏览器选项卡与资料持久化设计.md`
>
> 目标：把现有单页/独立弹窗浏览器演进为共享 Profile 的网页选项卡模块。
>
> 状态：三阶段实现完成；普通 Windows WebView2 Runtime `46/46` 通过。

## 阶段一：当前问题修复

1. 地址栏改为始终可编辑；导航完成只更新文本，不设置只读状态。
2. 地址栏获得焦点时全选；`Ctrl+L` 继续在网页模块内聚焦并全选。
3. 地址提交只调用当前标签的 `navigate`，不创建弹窗或新窗口。
4. 导航完成且 URL 合法时写入网页历史，按 URL 去重并保存访问时间。
5. 增加假后端/Qt Test：二次编辑、焦点全选、当前标签复用、成功导航历史。

## 阶段二：网页选项卡与共享 Profile

1. 提取 `BrowserTab` 值模型和 `BrowserTabController`，顶部 `QTabBar` 管理当前标签。
2. 后端把 Environment/Profile 提升为共享资源；按标签创建和关闭 WebView2 Controller。
3. `NewWindowRequested` 和原弹窗入口改为新标签请求；标签间共享 Profile 和会话。
4. 切换标签独立保存地址、标题、前进/后退状态；关闭最后一个标签保留空白页。
5. 增加 GUI/非 Runtime 接线测试和本机 Runtime 标签共享会话测试。

阶段二质量审计补充：

- 主/次标签均按 WebView2 NavigationId 绑定显式导航代次，拒绝晚到完成覆盖新地址。
- 普通 WebView2 事件强制经 Qt 队列投递；新窗口用 deferral 与稳定请求 ID 异步转标签。
- 标签独立恢复错误页、网页宿主和状态文字；关闭后台标签不清理当前页未决请求。

## 阶段三：历史、收藏夹与完整清除

1. 扩展浏览器专用 `BrowserDataStore`/`QSettingsBrowserDataStore`，增加历史与收藏项
   结构化数组，不进入播放器 `AppStateSnapshot`。
2. 网页工具栏增加历史/收藏入口、添加/编辑备注和删除动作。
3. 默认当前标签打开；Ctrl+点击/中键打开新标签。
4. 清除网页数据使用 `ALL_PROFILE`，覆盖 Cookie、LocalStorage、IndexedDB、缓存、
   已保存密码和自动填充；清除后重置网页标签，不删除 MediaHub 历史/收藏。
5. 增加跨重启 QSettings、Profile 登录资料和清除后资料消失测试；敏感字段扫描保持为零。

阶段三实现补充约束：

- 历史/收藏持久 URL 去除用户信息、查询和片段；真实导航 URL 不因此改写。
- 清除前关闭其他 Controller，只由当前真实 WebView 执行完整 Profile 清除和空白导航。
- 后台标签敏感请求默认拒绝，不把确认对话框显示到当前标签。
- 旧 `WebView2PopupWindow` 从生产目标和仓库实现中删除，新窗口只走 Tab Controller。
- 只有成功导航刷新访问时间；动态标题变化只更新已有历史标题，不改变排序与时间。

## 阶段验收与提交

- 每阶段先补 RED 测试，再实现 GREEN；Debug 分层 CTest 必须通过，Core-only 不受影响。
- 阶段完成后回填 `docs/02-开发交接.md` 和对应测试文档，再创建一个单目的本地提交。
- 远程认证可用且无冲突时推送；Runtime、真实登录和系统 UI 仍标记为需人工验收的边界。
