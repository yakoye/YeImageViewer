# 实况照片测试素材

同一段带音轨的视频，按三种封装各放一份，分别走查看器里三条不同的加载路径：

| 文件 | 封装 | 加载路径 |
| --- | --- | --- |
| `live-microvideo.jpg` | 谷歌 MicroVideo：JPEG 尾部附视频，XMP 的 `GCamera:MicroVideoOffset` 标出视频长度 | `loadMotionPhoto`，内嵌视频 |
| `live-photo.livp` | 苹果 iCloud 导出格式：zip 里一张 JPEG 加一段 MOV | `loadLivp` |
| `live-sidecar.jpg` + `live-sidecar.mov` | 苹果、VIVO 的同名配对：静态图旁边放同名视频 | `loadMotionPhoto`，同名视频文件 |

视频 3 秒、30 fps、640×480 H.264，声音 48 kHz 立体声 AAC。内容专为验证音画同步设计：

- 第 30 帧（t = 1.000 s）整帧纯白，其余帧是灰色渐变加一根移动的竖条；
- t = 1.000 s 起有一声 50 ms 的 1 kHz 响声，其余时间是轻微的 440 Hz 底音，让音频电平始终有读数。

`runTests.ps1` 用 `--decode-probe` 的导出功能取出解码后的时间轴（`frames.csv`）和声音（`audio.wav`），
要求最亮的一帧是第 30 帧、起于 1000 ms，响声起点与它相差不超过 15 ms。
`tools/image-test-runner/probe-live-photo-audio.ps1` 读系统音频会话电平，验证自动播放默认静音、
空格重播与悬停「实况」标记会真的出声。

## 重新生成

```powershell
./scripts/generate-live-photo-fixtures.ps1
```

需要 Visual Studio C++ 工具（编译 `tools/test-fixtures/make-motion-clip.cpp`）和 ImageMagick（生成静态图）。
视频由 Windows 自带的 Media Foundation 编码，不引入额外依赖。

注意：查看器会把小于 64 KiB 的视频当作无效数据跳过。生成器给画面加了逐帧变化的细噪点，
保证码流远大于这个门槛；脚本也会检查体积，太接近门槛时直接报错，而不是生成一份
只能测到"静态图"路径的素材。
