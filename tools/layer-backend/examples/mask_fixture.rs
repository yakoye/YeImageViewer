// 最小公开夹具生成器；与合成器输出无关，独立指定四个最终像素。
use photocraft_psd::{LayerSpec, MaskSpec, PixelData, PsdBuilder, Rect};
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let target = std::env::args().nth(1).ok_or("需要输出文件路径")?;
    let mut builder = PsdBuilder::new(2, 2);
    builder.push_layer(LayerSpec::new(
        "Blue",
        0,
        0,
        2,
        2,
        PixelData::Rgba8([0, 0, 255, 255].repeat(4)),
    ));
    let mut red = LayerSpec::new(
        "Masked red",
        0,
        0,
        2,
        2,
        PixelData::Rgba8([255, 0, 0, 255].repeat(4)),
    );
    red.mask = Some(MaskSpec {
        rect: Rect::from_xywh(0, 0, 2, 2),
        data: vec![0, 255, 128, 255],
        default_color: 255,
        disabled: false,
    });
    builder.push_layer(red);
    builder.composite(PixelData::Rgba8(vec![
        0, 0, 255, 255, 255, 0, 0, 255, 128, 0, 127, 255, 255, 0, 0, 255,
    ]));
    std::fs::write(target, builder.to_bytes()?)?;
    Ok(())
}
