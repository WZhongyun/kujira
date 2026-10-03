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

- 编译时这里的内容会被复制到程序旁边的 `models/` 文件夹。直接使用编译好的程序时，把模型文件夹放进程序旁边的 `models/` 即可（旧版的 `assets/models/` 也还能识别）。
- 程序会自动使用按名称排序的第一个模型；模型文件（`.model3.json` 等）直接放在 `models/` 里也可以。也可以在设置「外观与动画 > 模型文件夹」里指定任意路径，相对路径从程序所在文件夹算起。
- 找不到模型时会自动打开设置，显示查找过的位置；点「打开 models 文件夹」放入模型后再点「重新加载模型」。
- 文件夹里所有的 `*.exp3.json`（表情）和 `*.motion3.json`（动画，含一级子文件夹）都会被自动加载，即使 `model3.json` 没有登记（很多 VTube Studio 模型只在 `.vtube.json` 里登记它们）。
- 待机动画默认用名为 `idle` 的动画；找不到时读取 `.vtube.json` 里的 `IdleAnimation`。
- 默认的状态映射是按「DS鲸鱼娘」模型的表情和动画名配置的，换模型后在设置「状态映射」里重新选择即可。

模型文件不会提交到仓库（见 `.gitignore`）。请遵守模型作者的使用须知。
