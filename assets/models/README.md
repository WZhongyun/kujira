# 模型放这里

把 Live2D 模型文件夹复制到这个目录下，例如：

```
assets/models/
  Kujira-Live2D/
    c_0120.model3.json
    c_0120.moc3
    c_0120.2048/texture_00.png
    motions/idle.motion3.json
    ...
```

- 程序会自动使用这里按名称排序的第一个模型；也可以在设置「外观与动画 > 模型文件夹」里指定任意路径。
- 文件夹里所有的 `*.exp3.json`（表情）和 `*.motion3.json`（动作，含一级子文件夹）都会被自动加载，即使 `model3.json` 没有登记（很多 VTube Studio 模型只在 `.vtube.json` 里登记它们）。
- 待机动作默认用名为 `idle` 的动作；找不到时读取 `.vtube.json` 里的 `IdleAnimation`。
- 默认的动作映射是按「DS鲸鱼娘」模型的表情名配置的，换模型后在设置「动作映射」里重新选择即可。

模型文件不会提交到仓库（见 `.gitignore`）。请遵守模型作者的使用须知。
